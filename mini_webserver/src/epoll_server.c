/*
 * W2D5 epoll_server.c — 基于 epoll 的 HTTP 服务器 (Webserver V1.0)
 *
 * 功能：
 *   1. socket()    创建 TCP 监听套接字
 *   2. bind()      绑定 host:port
 *   3. listen()    进入监听状态
 *   4. epoll_create1()   创建 epoll 实例
 *   5. epoll_ctl()       注册 listen_fd 和所有 client_fd
 *   6. epoll_wait()      I/O 多路复用，等待就绪事件
 *   7. accept()    接受新客户端连接
 *   8. recv()      接收 HTTP 请求
 *   9. handle_request_string()  路由分发 + 生成 HTTP 响应
 *  10. send()      发送 HTTP 响应
 *  11. close()     关闭客户端连接
 *
 * 对照 W2D5 知识点：
 *   - epoll_create1() 创建 epoll 实例（代替 select 的 fd_set）
 *   - epoll_ctl() 的三种操作：EPOLL_CTL_ADD / MOD / DEL
 *   - epoll_wait() 等待就绪事件（代替 select()）
 *   - struct epoll_event：events 字段（EPOLLIN）和 data.fd
 *   - LT（电平触发）模式：默认模式，未处理完下次还会通知
 *   - HTTP 请求解析：method path protocol
 *   - 路由分发：/hello → 200, /users/<name> → 200, 其他 → 404
 *
 * 技术限制（验收标准）：
 *   - 未使用 select、多线程、多进程或线程池
 *   - 纯 epoll + 单线程事件循环
 */

#include "epoll_server.h"
#include "request_handler.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*
 * 添加新客户端到客户端列表并注册到 epoll
 *
 * 参数：
 *   clients      - 客户端信息数组
 *   client_count - 当前客户端数量（指针，会递增）
 *   conn_fd      - accept 返回的连接套接字
 *   client_addr  - 客户端地址信息
 *   epfd         - epoll 实例的文件描述符
 *
 * 返回值：
 *    0  - 成功添加
 *   -1  - 客户端列表已满
 */
static int add_client(client_info_t *clients, int *client_count,
                      int conn_fd, struct sockaddr_in *client_addr,
                      int epfd)
{
    int i;

    if (*client_count >= MAX_CLIENTS) {
        fprintf(stderr, "[SERVER] Max clients (%d) reached, rejecting new connection\n",
                MAX_CLIENTS);
        return -1;
    }

    /* 查找空闲槽位 */
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == -1) {
            clients[i].fd   = conn_fd;
            clients[i].port = ntohs(client_addr->sin_port);
            strncpy(clients[i].ip,
                    inet_ntoa(client_addr->sin_addr),
                    sizeof(clients[i].ip) - 1);
            clients[i].ip[sizeof(clients[i].ip) - 1] = '\0';
            clients[i].recv_buf[0] = '\0';
            clients[i].buf_len     = 0;

            (*client_count)++;

            /* ===== 验收标准：epoll_ctl ADD ===== */
            {
                struct epoll_event ev;
                ev.events = EPOLLIN;          /* LT 模式（默认），监听可读事件 */
                ev.data.fd = conn_fd;         /* 用 fd 作为标识 */
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev) < 0) {
                    perror("[SERVER] epoll_ctl(ADD) failed");
                }
            }

            printf("[SERVER] Connection #%d from %s:%d (fd=%d)\n",
                   *client_count, clients[i].ip, clients[i].port, conn_fd);
            return 0;
        }
    }

    fprintf(stderr, "[SERVER] No free slot (should not happen)\n");
    return -1;
}

/*
 * 移除客户端（请求处理完毕、客户端断开或出错）
 *
 * 关键操作（对应验收标准第 7 条）：
 *   1. epoll_ctl(EPOLL_CTL_DEL)  — 从 epoll 监听中移除
 *   2. close(client_fd)           — 关闭套接字
 *   3. clients[i].fd = -1         — 标记槽位为空闲
 *   4. client_count--             — 连接数减一
 */
static void remove_client(client_info_t *clients, int i, int *client_count,
                          int epfd, const char *reason)
{
    int fd = clients[i].fd;

    printf("[SERVER] Client %s:%d disconnected (fd=%d) — %s\n",
           clients[i].ip, clients[i].port, fd, reason);

    /* ===== 验收标准第 7 条：正确使用 epoll_ctl DEL ===== */
    if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        /* ENOENT 表示 fd 已经不在 epoll 中，可以忽略 */
        if (errno != ENOENT) {
            perror("[SERVER] epoll_ctl(DEL) failed");
        }
    }

    close(fd);                /* 关闭套接字，释放内核资源          */

    clients[i].fd = -1;       /* 标记槽位为空闲，供新连接复用      */
    (*client_count)--;         /* 连接计数减一                      */
}

/*
 * 找到 fd 对应的客户端数组索引
 */
static int find_client_by_fd(client_info_t *clients, int fd)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == fd) {
            return i;
        }
    }
    return -1;
}

/* ===== epoll_server_run() ============================================
 *
 * 主函数：启动基于 epoll() 的 HTTP 服务器 (Webserver V1.0)。
 *
 * 整体流程：
 *   socket() → bind() → listen() → epoll_create1() →
 *   注册 listen_fd 到 epoll →
 *   while (request_count < max_requests) {
 *       nfds = epoll_wait(epfd, events, MAX_EVENTS, -1)
 *       for (i = 0; i < nfds; i++) {
 *           if (events[i].data.fd == listen_fd) {
 *               accept() → add_client() → epoll_ctl(ADD)
 *           } else {
 *               recv() → handle_request_string() → send()
 *               remove_client() → epoll_ctl(DEL)
 *               request_count++
 *           }
 *       }
 *   }
 *   清理所有连接 → return
 *
 * 技术限制：
 *   - 纯 epoll + 单线程事件循环
 *   - 未使用 select、多线程、多进程或线程池
 * ==================================================================== */
int epoll_server_run(int port, int max_requests)
{
    int                  listen_fd;             /* 监听套接字描述符                */
    int                  epfd;                  /* epoll 实例文件描述符             */
    int                  client_count = 0;      /* 当前已连接的客户端数量           */
    int                  request_count = 0;     /* 已处理的 HTTP 请求数             */
    struct sockaddr_in   server_addr;           /* 服务器地址结构                  */
    struct sockaddr_in   client_addr;           /* 客户端地址结构（accept 时填充）  */
    socklen_t            client_addr_len;       /* 客户端地址长度                  */
    client_info_t        clients[MAX_CLIENTS];  /* 客户端信息数组                  */
    struct epoll_event   events[MAX_EVENTS];    /* epoll_wait 返回的事件数组       */
    int                  i;

    if (port <= 0) port = DEFAULT_PORT;
    if (max_requests <= 0) max_requests = 10;   /* 默认处理 10 个请求 */

    /* 禁用 stdout 缓冲，确保日志实时输出 */
    setbuf(stdout, NULL);

    /* 初始化客户端数组：所有槽位标记为 -1（空闲） */
    for (i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

    printf("=== W2D5 Webserver V1.0 (epoll) ===\n");
    printf("[SERVER] Max requests: %d\n", max_requests);

    /* ===== 步骤 1: socket() 创建 TCP 套接字 ===== */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("[SERVER] socket() failed");
        return -1;
    }
    printf("[SERVER] socket() created, fd=%d\n", listen_fd);

    /* SO_REUSEADDR：允许服务器重启时立即重用端口 */
    {
        int optval = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                       &optval, sizeof(optval)) < 0) {
            perror("[SERVER] setsockopt(SO_REUSEADDR) failed");
            close(listen_fd);
            return -1;
        }
    }

    /* ===== 步骤 2: bind() 绑定地址和端口 ===== */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("[SERVER] bind() failed");
        close(listen_fd);
        return -1;
    }
    printf("[SERVER] bind() to 0.0.0.0:%d\n", port);

    /* ===== 步骤 3: listen() 进入监听状态 ===== */
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("[SERVER] listen() failed");
        close(listen_fd);
        return -1;
    }
    printf("[SERVER] listen() on port %d\n", port);

    /* ===== 步骤 4: epoll_create1() 创建 epoll 实例 ===== */
    /*
     * ===== 验收标准第 8 条：正确使用 epoll_create1() =====
     */
    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("[SERVER] epoll_create1() failed");
        close(listen_fd);
        return -1;
    }
    printf("[SERVER] epoll_create1() succeeded, epfd=%d\n", epfd);

    /* 将 listen_fd 注册到 epoll */
    {
        struct epoll_event ev;
        ev.events = EPOLLIN;          /* LT 模式（默认），监听可读事件 */
        ev.data.fd = listen_fd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
            perror("[SERVER] epoll_ctl(ADD listen_fd) failed");
            close(epfd);
            close(listen_fd);
            return -1;
        }
    }
    printf("[SERVER] listen_fd registered to epoll\n");

    printf("\n[SERVER] Webserver V1.0 is running. Press Ctrl+C to stop.\n");
    printf("[SERVER] Process up to %d requests, then exit normally.\n\n", max_requests);

    /* ===== 步骤 5: 主事件循环 ===== */
    while (request_count < max_requests) {
        int nfds;
        int j;

        /*
         * ===== 验收标准第 8 条：正确使用 epoll_wait() =====
         */
        nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("[SERVER] epoll_wait() failed");
            break;
        }

        for (j = 0; j < nfds; j++) {
            int ready_fd = events[j].data.fd;

            /*
             * ===== 情况 A：listen_fd 可读 → 有新客户端连接 =====
             */
            if (ready_fd == listen_fd) {
                client_addr_len = sizeof(client_addr);
                int conn_fd = accept(listen_fd,
                                     (struct sockaddr *)&client_addr,
                                     &client_addr_len);
                if (conn_fd < 0) {
                    if (errno != EINTR) {
                        perror("[SERVER] accept() failed");
                    }
                } else {
                    if (add_client(clients, &client_count,
                                   conn_fd, &client_addr, epfd) != 0) {
                        close(conn_fd);
                    }
                }
            } else {
                /*
                 * ===== 情况 B：客户端 fd 可读 → HTTP 请求到达 =====
                 *
                 * 处理流程：
                 *   1. recv()  读取 HTTP 请求
                 *   2. handle_request_string()  解析 + 路由 + 生成响应
                 *   3. send()  发送 HTTP 响应
                 *   4. remove_client()  清理连接
                 *   5. request_count++
                 */
                int client_idx = find_client_by_fd(clients, ready_fd);
                if (client_idx == -1) {
                    continue; /* 找不到对应客户端，忽略 */
                }

                {
                    int client_fd = clients[client_idx].fd;
                    char recv_buf[RECV_BUF_SIZE];
                    char resp_buf[RESP_BUF_SIZE];
                    ssize_t n;

                    memset(recv_buf, 0, sizeof(recv_buf));
                    n = recv(client_fd, recv_buf, sizeof(recv_buf) - 1, 0);

                    if (n < 0) {
                        printf("[SERVER] recv() error on fd=%d: %s\n",
                               client_fd, strerror(errno));
                        remove_client(clients, client_idx, &client_count,
                                      epfd, "recv error");
                        continue;
                    }

                    if (n == 0) {
                        /*
                         * recv()==0 → 客户端关闭连接
                         * 在收到任何 HTTP 请求之前客户端就关闭了
                         */
                        printf("[SERVER] recv() returned 0 on fd=%d — client closed before request\n",
                               client_fd);
                        remove_client(clients, client_idx, &client_count,
                                      epfd, "client closed (no request)");
                        continue;
                    }

                    /* recv() > 0：收到 HTTP 请求 */
                    recv_buf[n] = '\0';
                    printf("[SERVER] recv() %zd bytes from %s:%d (fd=%d)\n",
                           n, clients[client_idx].ip,
                           clients[client_idx].port, client_fd);

                    /*
                     * ===== 处理 HTTP 请求 =====
                     * 使用现有的 handle_request_string() 函数：
                     *   /hello          → HTTP/1.1 200 OK + "Hello, Web!"
                     *   /users/<name>   → HTTP/1.1 200 OK + 用户信息
                     *   其他            → HTTP/1.1 404 Not Found
                     */
                    memset(resp_buf, 0, sizeof(resp_buf));
                    handle_request_string(recv_buf, resp_buf, sizeof(resp_buf));

                    /*
                     * ===== 发送 HTTP 响应 =====
                     */
                    {
                        ssize_t resp_len = strlen(resp_buf);
                        ssize_t sent = send(client_fd, resp_buf, resp_len, 0);
                        if (sent < 0) {
                            printf("[SERVER] send() response failed to %s:%d: %s\n",
                                   clients[client_idx].ip,
                                   clients[client_idx].port,
                                   strerror(errno));
                        } else {
                            printf("[SERVER] send() %zd bytes response to %s:%d (fd=%d)\n",
                                   sent, clients[client_idx].ip,
                                   clients[client_idx].port, client_fd);
                        }
                    }

                    request_count++;
                    printf("[SERVER] Request count: %d / %d\n",
                           request_count, max_requests);

                    /*
                     * ===== 清理客户端连接 =====
                     * 每个 HTTP 请求处理完毕后关闭连接（HTTP/1.0 语义）
                     */
                    remove_client(clients, client_idx, &client_count,
                                  epfd, "request completed");

                    /* 如果达到最大请求数，退出事件循环 */
                    if (request_count >= max_requests) {
                        printf("[SERVER] Max requests (%d) reached, shutting down...\n",
                               max_requests);
                        goto shutdown;
                    }
                }
            }
        }
    }

shutdown:
    /* ===== 清理：关闭所有客户端和监听套接字 ===== */
    printf("\n[SERVER] Shutting down...\n");
    printf("[SERVER] Total requests processed: %d\n", request_count);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1) {
            printf("[SERVER] Closing client %s:%d (fd=%d)\n",
                   clients[i].ip, clients[i].port, clients[i].fd);
            epoll_ctl(epfd, EPOLL_CTL_DEL, clients[i].fd, NULL);
            close(clients[i].fd);
            clients[i].fd = -1;
        }
    }
    close(epfd);
    close(listen_fd);
    printf("[SERVER] Server stopped normally.\n");

    return 0;
}
