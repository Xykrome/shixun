/*
 * V0.7 tcp_fork_server.c — 多进程并发 TCP 网络服务器
 *
 * 对照 W2D2 多进程并发服务器架构（Slide 2）：
 *
 *   int listen_fd = socket(...);
 *   bind(listen_fd, ...);
 *   listen(listen_fd, LISTENQ);
 *   for (;;) {
 *       conn_fd = accept(listen_fd, ...);
 *       if ((pid = fork()) == 0) {
 *           close(listen_fd);       // 子进程关闭监听套接字
 *           doit(conn_fd);          // 处理客户端请求
 *           close(conn_fd);         // 处理完毕关闭连接
 *           exit(0);                // 子进程终止
 *       }
 *       close(conn_fd);             // 父进程关闭已连接套接字
 *   }
 *
 * V0.7 新增知识点（相比 V0.6 单连接版本）：
 *   - fork()         为每个客户端连接创建子进程
 *   - SIGCHLD + waitpid(WNOHANG)  回收僵尸进程
 *   - errno == EINTR 处理         被信号打断时继续等待
 *   - SIGPIPE → SIG_IGN           客户端异常断开不终止服务器
 *   - 父子进程 fd 管理             父close(conn_fd) / 子close(listen_fd)
 */

#include "tcp_fork_server.h"
#include "request_handler.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG      5          /* 已完成连接队列最大长度  */
#define RECV_BUF_SIZE 8192      /* HTTP 请求接收缓冲区大小  */
#define RESP_BUF_SIZE 8192      /* HTTP 响应发送缓冲区大小  */
#define MAX_CLIENTS   10        /* 最大处理连接数，达到后退出（方便测试） */

/*
 * ===== SIGCHLD 信号处理函数 =====
 *
 * 子进程终止时，内核向父进程发送 SIGCHLD 信号。
 * 父进程在此处理函数中调用 waitpid() 回收子进程资源，避免僵尸进程。
 *
 * waitpid(-1, &stat, WNOHANG):
 *   - pid = -1     等待任意子进程
 *   - &stat        接收子进程终止状态
 *   - WNOHANG      非阻塞：无已终止子进程时立即返回 0
 *
 * 为什么用 while 循环 + WNOHANG？
 *   Unix 信号不排队。多个子进程几乎同时终止时，可能只触发一次 SIGCHLD。
 *   用 while 循环一次性回收所有已终止子进程，避免遗漏。
 */
static void sig_chld(int signo)
{
    pid_t pid;
    int   stat;

    (void)signo;  /* 参数未使用，避免编译警告 */

    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
        printf("[V0.7] Child process %d terminated (reaped)\n", pid);
    }
}

int tcp_fork_server_run(const server_config_t *config)
{
    int listen_fd;                       /* 监听套接字描述符       */
    int conn_fd;                         /* 连接套接字描述符       */
    struct sockaddr_in server_addr;      /* 服务器地址结构         */
    struct sockaddr_in client_addr;      /* 客户端地址结构         */
    socklen_t client_addr_len;           /* 客户端地址长度         */
    pid_t child_pid;                     /* 子进程 PID             */
    char recv_buf[RECV_BUF_SIZE];        /* 接收缓冲区             */
    char resp_buf[RESP_BUF_SIZE];        /* 响应缓冲区             */
    ssize_t n;
    char log_msg[512];
    int client_count = 0;                /* 已处理客户端计数       */

    /* ===== 注册信号处理函数 ===== */
    /*
     * SIGCHLD: 子进程终止信号，必须捕获以避免僵尸进程。
     *          在 listen() 之后、accept() 之前注册。
     *
     * SIGPIPE: 向已关闭的连接写数据时触发。
     *          默认行为是终止进程。设为 SIG_IGN 忽略此信号，
     *          send/write 将返回 -1 并设 errno = EPIPE，进程继续运行。
     */
    if (signal(SIGCHLD, sig_chld) == SIG_ERR) {
        log_error("signal(SIGCHLD) failed");
        perror("signal(SIGCHLD)");
        return -1;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        log_error("signal(SIGPIPE) failed");
        perror("signal(SIGPIPE)");
        return -1;
    }

    /* ===== 步骤 1: socket() 创建 TCP 套接字 ===== */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        log_error("socket() failed");
        perror("socket");
        return -1;
    }
    snprintf(log_msg, sizeof(log_msg),
             "socket() created, fd=%d", listen_fd);
    log_info(log_msg);

    /* SO_REUSEADDR: 重启时立即重用 TIME_WAIT 状态的端口 */
    {
        int optval = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                       &optval, sizeof(optval)) < 0) {
            log_error("setsockopt(SO_REUSEADDR) failed");
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
        log_error("bind() failed");
        perror("bind");
        close(listen_fd);
        return -1;
    }
    snprintf(log_msg, sizeof(log_msg),
             "bind() to %s:%d", config->host, config->port);
    log_info(log_msg);

    /* ===== 步骤 3: listen() 进入监听状态 ===== */
    if (listen(listen_fd, BACKLOG) < 0) {
        log_error("listen() failed");
        perror("listen");
        close(listen_fd);
        return -1;
    }
    snprintf(log_msg, sizeof(log_msg),
             "listen() on %s:%d, waiting for connections (fork mode)...",
             config->host, config->port);
    log_info(log_msg);
    printf("[V0.7] Multi-process TCP server listening on http://%s:%d\n",
           config->host, config->port);
    printf("[V0.7] Max clients: %d\n", MAX_CLIENTS);

    /* ===== 步骤 4: 主循环 — accept() → fork() → 子进程处理 → 父进程回收 =====
     *
     * 这是 W2D2 多进程并发服务器的核心架构：
     *
     *   1. accept()  阻塞等待客户端连接
     *   2. fork()    为每个客户端创建子进程
     *   3. 子进程：  处理 HTTP 请求 → 发送响应 → exit(0)
     *   4. 父进程：  close(conn_fd)，继续 accept() 下一个连接
     *
     * 三个进程的状态（W2D2 Slide 7）：
     *   - 客户端：   阻塞于 fgets（等待用户输入 / curl 发送请求）
     *   - 服务器子进程：阻塞于 read/recv（等待客户端发送 HTTP 请求）
     *   - 服务器父进程：阻塞于 accept（等待下一个客户端连接）
     */
    while (1) {
        client_addr_len = sizeof(client_addr);
        conn_fd = accept(listen_fd,
                         (struct sockaddr *)&client_addr,
                         &client_addr_len);

        if (conn_fd < 0) {
            /*
             * accept() 被信号打断的处理（W2D2 Slide 13-14）：
             *
             * 父进程阻塞于 accept() 时收到 SIGCHLD 信号 → accept 返回 -1
             * 并设 errno = EINTR。此时应 continue 继续等待，而非退出程序。
             *
             * 解决方式：
             *   if (errno == EINTR) continue;
             */
            if (errno == EINTR) {
                continue;          /* 被信号打断，重新 accept */
            }
            log_error("accept() failed");
            perror("accept");
            break;
        }

        client_count++;
        snprintf(log_msg, sizeof(log_msg),
                 "accept() connection #%d from %s:%d",
                 client_count,
                 inet_ntoa(client_addr.sin_addr),
                 ntohs(client_addr.sin_port));
        log_info(log_msg);
        printf("[V0.7] Connection #%d from %s:%d\n",
               client_count,
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        /* ===== fork() 创建子进程处理该连接 =====
         *
         * fork() 的返回值：
         *   > 0 — 在父进程中，返回子进程 PID
         *   = 0 — 在子进程中
         *   < 0 — 创建失败
         *
         * fork 后文件描述符的复制：
         *   父进程和子进程各自持有 listen_fd 和 conn_fd 的副本。
         *   - 父进程应立即 close(conn_fd) —— 父进程不需要与客户端通信
         *   - 子进程应立即 close(listen_fd) —— 子进程不需要接受新连接
         *
         * 如果父进程不关闭 conn_fd：
         *   子进程 close(conn_fd) 后该套接字引用计数仍为 1，
         *   TCP FIN 不会发出，客户端连接无法正常关闭。
         */
        child_pid = fork();

        if (child_pid < 0) {
            log_error("fork() failed");
            perror("fork");
            close(conn_fd);
            continue;
        }

        if (child_pid == 0) {
            /* =========================================================
             * 子进程分支 — 处理单个 HTTP 请求
             * ========================================================= */
            close(listen_fd);    /* 子进程不需要监听，关闭之 */

            snprintf(log_msg, sizeof(log_msg),
                     "Child PID=%d handling request from %s:%d",
                     getpid(),
                     inet_ntoa(client_addr.sin_addr),
                     ntohs(client_addr.sin_port));
            log_info(log_msg);

            /* ===== 接收 HTTP 请求 ===== */
            memset(recv_buf, 0, sizeof(recv_buf));
            n = recv(conn_fd, recv_buf, sizeof(recv_buf) - 1, 0);
            if (n < 0) {
                log_error("recv() failed in child");
                perror("recv");
                close(conn_fd);
                exit(1);
            }
            if (n == 0) {
                log_info("Client closed connection before sending data");
                close(conn_fd);
                exit(0);
            }
            recv_buf[n] = '\0';

            snprintf(log_msg, sizeof(log_msg),
                     "recv() received %zd bytes", n);
            log_info(log_msg);
            printf("[V0.7 Child %d] Received request (%zd bytes)\n",
                   getpid(), n);

            /* ===== 处理 HTTP 请求，生成响应 ===== */
            memset(resp_buf, 0, sizeof(resp_buf));
            handle_request_string(recv_buf, resp_buf, sizeof(resp_buf));

            snprintf(log_msg, sizeof(log_msg),
                     "Response generated (%zd bytes)", strlen(resp_buf));
            log_info(log_msg);

            /* ===== 发送 HTTP 响应 ===== */
            n = send(conn_fd, resp_buf, strlen(resp_buf), 0);
            if (n < 0) {
                /*
                 * SIGPIPE 已被忽略，客户端异常断开时 send 返回 -1、
                 * errno = EPIPE，不会导致整个服务器进程崩溃。
                 */
                log_error("send() failed in child (client may have disconnected)");
                perror("send");
            } else {
                snprintf(log_msg, sizeof(log_msg),
                         "send() sent %zd bytes", n);
                log_info(log_msg);
                printf("[V0.7 Child %d] Sent response (%zd bytes)\n",
                       getpid(), n);
            }

            /* ===== 子进程退出 =====
             *
             * close(conn_fd) → 引发 TCP 四次挥手（FIN → ACK, FIN → ACK）
             * exit(0)        → 子进程终止，内核向父进程发送 SIGCHLD
             */
            close(conn_fd);
            log_info("Child done, exiting");
            exit(0);
            /* =========================================================
             * 子进程分支结束
             * ========================================================= */
        }

        /* =============================================================
         * 父进程分支
         * ============================================================= */
        /*
         * 父进程关闭 conn_fd：子进程已持有 conn_fd 副本用于通信，
         * 父进程必须关闭自己的副本，否则 TCP 连接无法正常终止。
         */
        close(conn_fd);
        printf("[V0.7] Forked child PID=%d for connection #%d, "
               "back to listening...\n",
               child_pid, client_count);

        /*
         * 达到 max_clients 上限后停止 accept() 循环，退出服务器。
         * 这是为自动测试设计的机制，实际生产环境通常无限循环。
         */
        if (client_count >= MAX_CLIENTS) {
            printf("[V0.7] Reached max_clients (%d), stopping accept loop\n",
                   MAX_CLIENTS);
            break;
        }
    }

    /* ===== 步骤 5: 关闭监听套接字，等待剩余子进程 ===== */
    close(listen_fd);
    log_info("listen_fd closed, server exiting normally");

    /*
     * 服务器退出前最后回收一次所有子进程。
     * 此时不再 accept 新连接，待所有子进程处理完毕后退出。
     */
    {
        pid_t pid;
        int   stat;
        while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
            printf("[V0.7] Final reaping: child %d terminated\n", pid);
        }
    }

    printf("[V0.7] Multi-process server exiting normally\n");
    return 0;
}
