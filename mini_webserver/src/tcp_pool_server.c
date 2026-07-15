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

    /* ===== 忽略 SIGPIPE =====*/
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

    /* ===== 步骤 5: 主循环 — accept() → 入队 =====*/
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

     * ===== 步骤 7: 关闭线程池 =====
    thread_pool_destroy();

    printf("[V0.8] Thread-pool server exiting normally\n");
    log_info("[V0.8] Thread-pool server exiting normally");
    return 0;
}
