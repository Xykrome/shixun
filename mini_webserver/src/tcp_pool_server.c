/*
 * V0.8 tcp_pool_server.c — 线程池 TCP 网络服务器
 *
 * 对照 W2D3 线程池并发服务器架构：
 *
 *   listen_fd -> accept() -> client_fd -> task queue -> worker -> handler -> close(client_fd)
 *
 * 完整流程（对照指导书第5页）：
 *   ① listen_fd：主线程创建监听套接字，绑定 IP 和端口
 *   ② accept()：主线程接收客户端连接，返回 client_fd
 *   ③ task queue：主线程将 client_fd 放入任务队列
 *   ④ worker：线程池中的 worker 线程从队列取出 client_fd
 *   ⑤ handler：worker 调用 handle_request_string 处理 HTTP 请求
 *   ⑥ close(client_fd)：worker 关闭客户端连接
 *
 * V0.8 与之前版本的对比：
 *   V0.6: 单连接，accept 一次处理一次就退出
 *   V0.7: 多进程，fork 子进程处理，SIGCHLD + waitpid 回收
 *   V0.8: 线程池，固定数量 worker 线程复用，mutex + cond 同步
 */

#include "tcp_pool_server.h"
#include "thread_pool.h"
#include "request_handler.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG      5          /* 已完成连接队列最大长度  */
#define MAX_CLIENTS  10         /* 最大处理连接数，达到后退出 */

int tcp_pool_server_run(const server_config_t *config, int num_workers)
{
    int listen_fd;                       /* 监听套接字描述符 */
    struct sockaddr_in server_addr;      /* 服务器地址结构 */
    struct sockaddr_in client_addr;      /* 客户端地址结构 */
    socklen_t client_addr_len;           /* 客户端地址长度 */
    int client_count = 0;                /* 已处理客户端计数 */
    char log_msg[512];

    if (num_workers < 1)
        num_workers = 4;  /* 默认 4 个 worker */

    /* ===== 忽略 SIGPIPE =====
     *
     * 向已关闭的连接写数据时触发 SIGPIPE，默认行为是终止进程。
     * 设为 SIG_IGN 忽略此信号，send/write 将返回 -1 并设 errno = EPIPE。
     * （V0.7 也需要这个，V0.8 同样需要）
     */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        log_error("[V0.8] signal(SIGPIPE) failed");
        perror("signal(SIGPIPE)");
        return -1;
    }

    /* ===== 步骤 1: socket() 创建 TCP 套接字 ===== */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        log_error("[V0.8] socket() failed");
        perror("socket");
        return -1;
    }
    snprintf(log_msg, sizeof(log_msg),
             "[V0.8] socket() created, fd=%d", listen_fd);
    log_info(log_msg);

    /* SO_REUSEADDR：允许服务器重启时立即重用端口 */
    {
        int optval = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                       &optval, sizeof(optval)) < 0) {
            log_error("[V0.8] setsockopt(SO_REUSEADDR) failed");
            perror("setsockopt");
            close(listen_fd);
            return -1;
        }
    }

    /* ===== 步骤 2: bind() 绑定本地地址和端口 ===== */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(config->port);
    server_addr.sin_addr.s_addr = inet_addr(config->host);

    if (bind(listen_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        log_error("[V0.8] bind() failed");
        perror("bind");
        close(listen_fd);
        return -1;
    }
    snprintf(log_msg, sizeof(log_msg),
             "[V0.8] bind() to %s:%d", config->host, config->port);
    log_info(log_msg);

    /* ===== 步骤 3: listen() 进入监听状态 ===== */
    if (listen(listen_fd, BACKLOG) < 0) {
        log_error("[V0.8] listen() failed");
        perror("listen");
        close(listen_fd);
        return -1;
    }
    snprintf(log_msg, sizeof(log_msg),
             "[V0.8] listen() on %s:%d, waiting for connections (thread pool mode)...",
             config->host, config->port);
    log_info(log_msg);
    printf("[V0.8] Thread-pool TCP server listening on http://%s:%d\n",
           config->host, config->port);
    printf("[V0.8] Workers: %d, Max clients: %d\n", num_workers, MAX_CLIENTS);

    /* ===== 步骤 4: 初始化线程池 =====
     *
     * 预创建 num_workers 个 worker 线程。
     * 所有 worker 启动后立即阻塞在条件变量上等待任务。
     * （对照指导书第20页：预先创建线程 → 放入池中 → 等待任务）
     */
    if (thread_pool_init(num_workers) != 0) {
        log_error("[V0.8] Failed to initialize thread pool");
        close(listen_fd);
        return -1;
    }

    /* ===== 步骤 5: 主循环 — accept() → 入队 =====
     *
     * 这是 W2D3 线程池并发服务器的核心架构：
     *   1. accept() 阻塞等待客户端连接
     *   2. 将 client_fd 放入任务队列（生产者角色）
     *   3. 条件变量唤醒一个阻塞的 worker（消费者角色）
     *   4. worker 处理完毕后关闭 client_fd
     *
     * 与 V0.7 的关键区别：
     *   V0.7: accept → fork → 子进程处理 → 父进程 close(conn_fd) → 继续 accept
     *   V0.8: accept → 入队（主线程不 close） → worker 处理并 close → 继续 accept
     */
    while (1) {
        client_addr_len = sizeof(client_addr);
        int conn_fd = accept(listen_fd,
                             (struct sockaddr *)&client_addr,
                             &client_addr_len);

        if (conn_fd < 0) {
            /*
             * accept() 被信号打断的处理：
             * 服务器阻塞于 accept() 时收到信号 → accept 返回 -1
             * 并设 errno = EINTR。此时应 continue 继续等待。
             */
            if (errno == EINTR) {
                continue;
            }
            log_error("[V0.8] accept() failed");
            perror("accept");
            break;
        }

        client_count++;
        snprintf(log_msg, sizeof(log_msg),
                 "[V0.8] accept() connection #%d from %s:%d, fd=%d",
                 client_count,
                 inet_ntoa(client_addr.sin_addr),
                 ntohs(client_addr.sin_port),
                 conn_fd);
        log_info(log_msg);
        printf("[V0.8] Connection #%d from %s:%d (fd=%d)\n",
               client_count,
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               conn_fd);

        /* 将 client_fd 放入任务队列（生产者 → 队列 → 消费者）
         *
         * 与 V0.7 的区别：V0.7 主线程在此处 fork 子进程；
         * V0.8 主线程只负责分发任务，将 client_fd 交给 worker 处理，
         * 主线程不 close(client_fd)——由 worker 在处理完毕后关闭。
         */
        if (thread_pool_add_task(conn_fd) != 0) {
            snprintf(log_msg, sizeof(log_msg),
                     "[V0.8] Task queue full, rejecting connection #%d", client_count);
            log_error(log_msg);
            printf("[V0.8] Queue full! Rejecting connection #%d\n", client_count);
            close(conn_fd);  /* 队列满时主线程关闭该连接 */
        }

        /*
         * 达到 max_clients 上限后停止 accept() 循环。
         * 注意：已入队的任务可能还在处理中，worker 会继续完成它们。
         */
        if (client_count >= MAX_CLIENTS) {
            printf("[V0.8] Reached max_clients (%d), stopping accept loop\n",
                   MAX_CLIENTS);
            snprintf(log_msg, sizeof(log_msg),
                     "[V0.8] Reached max_clients (%d), stopping accept loop",
                     MAX_CLIENTS);
            log_info(log_msg);
            break;
        }
    }

    /* ===== 步骤 6: 关闭监听套接字 ===== */
    close(listen_fd);
    log_info("[V0.8] listen_fd closed");

    /*
     * ===== 步骤 7: 关闭线程池 =====
     *
     * 设置 shutdown 标志 → 广播条件变量唤醒所有 worker →
     * pthread_join 等待所有 worker 退出 → 清理 mutex/cond。
     *
     * 对照指导书第46页功能要求：
     *   "关闭线程池，唤醒 worker，等待所有线程退出"
     */
    thread_pool_destroy();

    printf("[V0.8] Thread-pool server exiting normally\n");
    log_info("[V0.8] Thread-pool server exiting normally");
    return 0;
}
