/*
 * W2D2 训练1：面向多客户连接的TCP文本聊天程序 —— 服务器端
 *
 * 功能：
 *   1. 创建TCP套接字，绑定端口，进入监听状态
 *   2. 循环 accept() 等待客户端连接（单进程版本仅处理一个客户端）
 *   3. accept() 成功后 fork() 子进程处理该客户端的文本通信
 *   4. 子进程与客户端交互（recv/send），收到 "exit" 后子进程退出
 *   5. 父进程关闭conn_fd，继续 accept() 等待下一个客户端
 *   6. 注册 SIGCHLD 信号处理函数，使用 waitpid(-1, &stat, WNOHANG) 回收僵尸进程
 *   7. accept() 被信号打断时判断 errno == EINTR，继续等待（而非退出）
 *   8. 忽略 SIGPIPE 信号，防止客户端异常断开导致服务器进程终止
 *
 * W2D2 新增知识点（相比 Day6 单连接版本）：
 *   - fork()         创建子进程，实现多客户端并发处理
 *   - SIGCHLD 信号    子进程终止时内核发送给父进程的信号
 *   - waitpid()      回收子进程，避免僵尸进程
 *   - WNOHANG        非阻塞等待，没有已终止子进程时立即返回
 *   - EINTR          慢系统调用（accept）被信号打断的错误码
 *   - SIGPIPE        向已收到RST的套接字写数据时触发的信号
 *   - signal()       注册信号处理函数（Posix简易接口）
 *
 * 使用方法：
 *   ./server
 *   （默认监听 0.0.0.0:8080）
 */

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

#define PORT       8080          /* 服务器监听端口 */
#define BUFSIZE    1024          /* 收发缓冲区大小 */
#define BACKLOG    5             /* 已完成连接队列最大长度 */
#define MAX_CLIENTS 10           /* 最大处理客户端数（方便测试），达到后退出 */

/* ===== SIGCHLD 信号处理函数 ===== */
/*
 * 当子进程终止时，内核向父进程发送 SIGCHLD 信号。
 * 父进程在此处理函数中调用 waitpid() 回收子进程，避免僵尸进程。
 *
 * waitpid(-1, &stat, WNOHANG):
 *   pid     = -1 → 等待任意子进程（不指定PID）
 *   stat    → 接收子进程的终止状态
 *   options = WNOHANG → 非阻塞，没有已终止子进程时立即返回0（而非阻塞等待）
 *
 * 使用 while 循环 + WNOHANG：
 *   由于 Unix 信号不排队，多个子进程几乎同时终止时可能只触发一次 SIGCHLD。
 *   因此用 while 循环一次回收所有已终止的子进程，避免遗漏僵尸进程。
 */
void sig_chld(int signo)
{
    pid_t pid;
    int   stat;

    (void)signo;  /* 避免编译警告：信号处理函数的参数在函数体内未使用 */

    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
        printf("[服务器] 子进程 %d 已终止（回收成功）\n", pid);
    }
}

int main(void)
{
    int listen_fd, conn_fd;                        /* 监听套接字 / 连接套接字 */
    struct sockaddr_in server_addr;                 /* 服务器地址结构 */
    struct sockaddr_in client_addr;                 /* 客户端地址结构 */
    socklen_t client_addr_len;                      /* 客户端地址结构长度 */
    pid_t child_pid;                                /* 子进程PID */
    int   client_count = 0;                         /* 已处理客户端计数 */

    /* ===== 注册信号处理函数 ===== */
    /*
     * signal(signo, handler)
     * SIGCHLD: 子进程终止时内核发送给父进程的信号，必须捕获以避免僵尸进程
     * SIGPIPE: 向已关闭的连接写数据时触发，默认行为是终止进程
     *          设置为 SIG_IGN 则忽略此信号，write/send 返回 EPIPE 错误
     */
    if (signal(SIGCHLD, sig_chld) == SIG_ERR) {
        perror("signal(SIGCHLD) 失败");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal(SIGPIPE) 失败");
        exit(EXIT_FAILURE);
    }

    /* ===== 步骤1: socket() 创建套接字 ===== */
    /*
     * socket(AF_INET, SOCK_STREAM, 0)
     *   AF_INET      — IPv4
     *   SOCK_STREAM  — 字节流（TCP），可靠、有序、双向
     *   0            — 自动选择协议（→ IPPROTO_TCP）
     */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket() 失败");
        exit(EXIT_FAILURE);
    }
    printf("[服务器] 套接字创建成功，fd = %d\n", listen_fd);

    /* SO_REUSEADDR: 允许服务器重启时立即重用端口（避免 TIME_WAIT 等待） */
    {
        int optval = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                       &optval, sizeof(optval)) < 0) {
            perror("setsockopt(SO_REUSEADDR) 失败");
            close(listen_fd);
            exit(EXIT_FAILURE);
        }
    }

    /* ===== 步骤2: bind() 绑定本地地址和端口 ===== */
    /*
     * bind(sockfd, (struct sockaddr*)&addr, addrlen)
     * 将套接字与 IP:Port 绑定，服务器需绑定众所周知的端口方便客户端连接
     *
     * sockaddr_in:
     *   sin_family = AF_INET     — IPv4
     *   sin_port   = htons(PORT) — 端口号，主机字节序→网络字节序（大端）
     *   sin_addr   = INADDR_ANY  — 绑定本机所有网络接口
     */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind() 失败");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    printf("[服务器] 绑定端口 %d 成功\n", PORT);

    /* ===== 步骤3: listen() 进入监听状态 ===== */
    /*
     * listen(sockfd, backlog)
     * 将主动套接字转为被动套接字（CLOSED → LISTEN）
     * backlog: 已完成连接队列 + 未完成连接队列的最大长度
     */
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen() 失败");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    printf("[服务器] 开始监听（backlog=%d），等待客户端连接...\n\n", BACKLOG);

    /* ===== 步骤4: 主循环 — accept → fork → 处理 ===== */
    /*
     * 多进程并发服务器核心架构（W2D2 Slide 2）：
     *
     *   for ( ; ; ) {
     *       conn_fd = accept(listen_fd, ...);   // 阻塞等待客户端连接
     *       pid = fork();                       // 创建子进程
     *       if (pid == 0) {                     // 子进程
     *           close(listen_fd);               // 子进程关闭监听套接字
     *           do_handle(conn_fd);             // 处理客户端请求
     *           close(conn_fd);                 // 处理完毕关闭连接
     *           exit(0);                        // 子进程退出
     *       }
     *       close(conn_fd);                     // 父进程关闭连接套接字
     *   }
     */
    while (1) {
        /* ---- 4a: accept() 接受客户端连接 ---- */
        client_addr_len = sizeof(client_addr);
        conn_fd = accept(listen_fd,
                         (struct sockaddr *)&client_addr,
                         &client_addr_len);

        if (conn_fd < 0) {
            /*
             * accept() 被信号打断时的处理
             *
             * 父进程阻塞于 accept()（慢系统调用）时，若收到 SIGCHLD 信号，
             * 信号处理函数执行完毕后，accept() 返回 -1，errno = EINTR。
             *
             * 此时应继续循环等待，而非当作致命错误退出。
             */
            if (errno == EINTR) {
                continue;          /* 被信号打断，重新 accept */
            }
            perror("accept() 失败");
            break;
        }

        client_count++;
        printf("[服务器] 客户端 #%d: %s:%d 已连接，fd = %d\n",
               client_count,
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               conn_fd);

        /* ---- 4b: fork() 创建子进程处理客户端 ---- */
        /*
         * fork() 创建一个与父进程几乎完全相同的子进程。
         * 返回值：
         *   >0  — 在父进程中返回，值为子进程的PID
         *   =0  — 在子进程中返回
         *   <0  — 创建失败
         *
         * fork() 后父子进程的文件描述符表是复制的：
         *   - 父进程持有 listen_fd 和 conn_fd
         *   - 子进程也持有 listen_fd 和 conn_fd（副本）
         *   - 父进程应 close(conn_fd)，因为父进程不需要与客户端通信
         *   - 子进程应 close(listen_fd)，因为子进程不需要接受新连接
         */
        child_pid = fork();

        if (child_pid < 0) {
            perror("fork() 失败");
            close(conn_fd);
            continue;
        }

        if (child_pid == 0) {
            /* =========================================================
             * 子进程分支
             * ========================================================= */
            char buf[BUFSIZE];
            ssize_t n;

            close(listen_fd);    /* 子进程不需要监听，关闭之 */

            printf("[子进程 %d] 开始服务客户端 %s:%d\n",
                   getpid(),
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));

            /* 给客户端发送欢迎信息 */
            const char *welcome = "[服务器] 连接成功！开始聊天（输入 'exit' 退出）\n";
            send(conn_fd, welcome, strlen(welcome), 0);

            /* ---- 子进程通信循环 ---- */
            while (1) {
                memset(buf, 0, BUFSIZE);

                /* 接收客户端消息 */
                n = recv(conn_fd, buf, BUFSIZE - 1, 0);
                if (n <= 0) {
                    if (n == 0) {
                        printf("[子进程 %d] 客户端正常断开连接\n", getpid());
                    } else {
                        /*
                         * SIGPIPE 已被设为 SIG_IGN，客户端异常断开后
                         * recv 返回错误，不会导致进程终止
                         */
                        printf("[子进程 %d] 接收错误（客户端可能异常断开）\n", getpid());
                    }
                    break;
                }
                buf[n] = '\0';

                /* 去掉尾部的 \r 和 \n */
                {
                    size_t len = strlen(buf);
                    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
                        buf[--len] = '\0';
                    }
                }

                printf("[子进程 %d] 收到来自 %s:%d 的消息: %s\n",
                       getpid(),
                       inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port),
                       buf);

                /* 收到 "exit" → 子进程退出 */
                if (strcmp(buf, "exit") == 0) {
                    printf("[子进程 %d] 收到 exit，准备退出\n", getpid());

                    /* 给客户端回复确认 */
                    const char *bye = "[服务器] 收到 exit，再见！\n";
                    send(conn_fd, bye, strlen(bye), 0);
                    break;
                }

                /* 将消息原样回射给客户端（echo 模式） */
                /*
                 * 也可改为从标准输入读取服务器管理员的回复。
                 * 此处采用 echo 模式方便演示多客户端并发：
                 * 同时启动多个客户端，服务器子进程分别回射各自的消息。
                 */
                {
                    char echo_buf[BUFSIZE + 32];
                    snprintf(echo_buf, sizeof(echo_buf),
                             "[服务器回射] %s", buf);
                    if (send(conn_fd, echo_buf, strlen(echo_buf), 0) < 0) {
                        printf("[子进程 %d] 发送失败（客户端可能已断开）\n", getpid());
                        break;
                    }
                }
            }

            /* 子进程退出处理 */
            close(conn_fd);
            printf("[子进程 %d] 服务结束，退出\n", getpid());
            exit(0);
            /* =========================================================
             * 子进程分支结束
             * ========================================================= */
        }

        /* =============================================================
         * 父进程分支
         * ============================================================= */
        /*
         * 父进程关闭 conn_fd（父进程不需要与客户端通信），
         * 然后回到循环顶部继续 accept() 等待下一个客户端。
         *
         * 重要：父进程必须关闭 conn_fd！
         * 如果父进程不关闭，子进程关闭后该套接字的引用计数不会归零，
         * TCP 连接无法正常终止（FIN 不会发出），客户端会一直等待。
         */
        close(conn_fd);
        printf("[父进程] conn_fd=%d 已关闭（交给子进程 %d 处理），继续等待下一个连接...\n\n",
               conn_fd, child_pid);

        /*
         * 达到最大客户端数后退出，方便自动测试。
         * 实际生产环境中服务器通常无限循环。
         */
        if (client_count >= MAX_CLIENTS) {
            printf("[服务器] 已处理 %d 个客户端，达到上限，退出\n", MAX_CLIENTS);
            break;
        }
    }

    /* ===== 步骤5: 关闭监听套接字 ===== */
    close(listen_fd);
    printf("[服务器] 监听套接字已关闭，程序结束\n");

    return 0;
}
