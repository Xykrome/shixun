/*
 * W2D5 epoll_server.c — 基于 epoll() 的多客户端聊天服务器
 *
 * 功能：
 *   1. socket()   创建 TCP 监听套接字
 *   2. bind()     绑定 host:port
 *   3. listen()   进入监听状态
 *   4. epoll_create1()  创建 epoll 实例
 *   5. epoll_ctl()      注册 listen_fd 和所有 client_fd
 *   6. epoll_wait()     I/O 多路复用，等待就绪事件
 *   7. accept()   接受新客户端连接（epoll 保证可读）
 *   8. recv()     接收客户端消息（按 \n 分帧，处理粘包/半包）
 *   9. send()     将消息回显给对应客户端
 *  10. close()    关闭客户端连接
 *
 * 对照 W2D5 知识点：
 *   - epoll_create1() 创建 epoll 实例（代替 select 的 fd_set）
 *   - epoll_ctl() 的三种操作：EPOLL_CTL_ADD / MOD / DEL
 *   - epoll_wait() 等待就绪事件（代替 select()）
 *   - struct epoll_event：events 字段（EPOLLIN）和 data.fd
 *   - LT（电平触发）模式：默认模式，未处理完下次还会通知
 *   - 基于 \n 的消息分帧：TCP 是字节流，一次 recv() 不等于一条消息
 *   - 客户端断开 vs 主动退出的处理差异（recv()==0 vs quit\n）
 */

#include "epoll_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ===== 消息分帧处理 ====================================================
 *
 * TCP 是字节流协议，没有消息边界。一次 recv() 可能收到：
 *   情况 A: "hello\n"            — 一条完整消息（刚好一个 \n）
 *   情况 B: "hel"                — 半条消息（还没收到 \n）
 *   情况 C: "hello\nworld\n"     — 两条消息粘在一起（一次收到多个 \n）
 *   情况 D: "hello\nwor"         — 一条完整 + 半条消息
 *
 * extract_messages() 函数处理以上所有情况：
 *   1. 将新收到的数据追加到 client->recv_buf 末尾
 *   2. 在 recv_buf 中查找 \n
 *   3. 找到一个 \n → 提取它前面的内容作为一条完整消息
 *   4. 将剩余数据移到缓冲区开头，继续累积
 *   5. 返回提取到的消息条数
 *
 * 返回值：提取到的完整消息条数（0 表示还没有完整消息）
 *
 * msg_list[] 必须由调用者提供，每个元素指向 recv_buf 中的位置
 * （不需要额外分配内存，消息以 \0 临时结尾）
 * ==================================================================== */
static int extract_messages(client_info_t *client, char *msg_list[], int max_msgs)
{
    int msg_count = 0;
    char *buf = client->recv_buf;
    int   len = client->buf_len;

    while (msg_count < max_msgs) {
        /* 在缓冲区中查找第一个 \n */
        char *nl = memchr(buf, '\n', len);
        if (nl == NULL) {
            /* 没有找到 \n，剩余数据保留在缓冲区中等待下次 recv */
            break;
        }

        /* 找到 \n，计算消息长度（不含 \n） */
        int msg_len = nl - buf;

        /* 提取消息：将 \n 替换为 \0 作为字符串结尾 */
        *nl = '\0';

        /* 跳过可能的 \r（兼容 Windows 风格的 \r\n） */
        if (msg_len > 0 && buf[msg_len - 1] == '\r') {
            buf[msg_len - 1] = '\0';
            msg_len--;
        }

        /* 跳过空行（单独的 \n 或 \r\n） */
        if (msg_len > 0) {
            msg_list[msg_count] = buf;
            msg_count++;
        }

        /* 移动指针到 \n 之后，继续查找下一条消息 */
        int consumed = (nl - buf) + 1;  /* +1 跳过 \n */
        buf  += consumed;
        len  -= consumed;
    }

    /* 将剩余未处理的数据移到缓冲区开头 */
    if (buf != client->recv_buf && len > 0) {
        memmove(client->recv_buf, buf, len);
    }
    client->buf_len = len;

    return msg_count;
}

/*
 * 添加新客户端到客户端列表
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

            /* ===== 验收标准第 5 条：epoll_ctl ADD ===== */
            {
                struct epoll_event ev;
                ev.events = EPOLLIN;          /* LT 模式（默认），监听可读事件 */
                ev.data.fd = conn_fd;         /* 用 fd 作为标识 */
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev) < 0) {
                    perror("[SERVER] epoll_ctl(ADD) failed");
                    /* 即使 epoll_ctl 失败，客户端仍在列表中，后续会清理 */
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
 * 移除客户端（无论是 quit\n、recv()==0、还是 recv 出错）
 *
 * 关键操作（对应验收标准第 4 条）：
 *   1. epoll_ctl(EPOLL_CTL_DEL)  — 从 epoll 监听中移除
 *   2. close(client_fd)           — 关闭套接字
 *   3. clients[i].fd = -1         — 标记槽位为空闲
 *   4. client_count--             — 连接数减一
 *
 * 参数：
 *   i            - 客户端在数组中的索引
 *   reason       - 断开原因（用于日志输出）
 */
static void remove_client(client_info_t *clients, int i, int *client_count,
                          int epfd, const char *reason)
{
    int fd = clients[i].fd;

    printf("[SERVER] Client %s:%d disconnected (fd=%d) — %s\n",
           clients[i].ip, clients[i].port, fd, reason);

    /* ===== 验收标准第 4 条：正确使用 epoll_ctl DEL ===== */
    if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        /* ENOENT 表示 fd 已经不在 epoll 中，可以忽略 */
        if (errno != ENOENT) {
            perror("[SERVER] epoll_ctl(DEL) failed");
        }
    }

    close(fd);                /* 关闭套接字，释放内核资源          */

    clients[i].fd = -1;       /* 标记槽位为空闲，供新连接复用      */
    (*client_count)--;         /* 连接计数减一                      */

    printf("[SERVER] Active connections: %d\n", *client_count);
}

/*
 * 处理来自客户端的消息
 *
 * 检查是否是 quit\n（验收标准第 4 条），
 * 否则显示消息内容。
 *
 * 参数：
 *   client  - 指向客户端信息结构体的指针
 *   msg     - 消息内容（以 \0 结尾，不含 \n）
 *
 * 返回值：
 *    1  - 客户端请求退出（quit）
 *    0  - 普通消息
 */
static int handle_client_message(client_info_t *client, const char *msg)
{
    /* ===== 验收标准第 4 条：quit\n 退出 ===== */
    if (strcmp(msg, "quit") == 0) {
        printf("[SERVER] Client %s:%d sent quit\n",
               client->ip, client->port);
        return 1;  /* 通知调用者关闭此客户端 */
    }

    /* 普通消息：显示在服务器终端 */
    printf("[MSG from %s:%d] %s\n",
           client->ip, client->port, msg);

    /*
     * 回显确认给客户端：
     * 客户端需要看到服务器的响应才知道消息已被处理。
     * 回显格式："[SERVER] received: <原消息>\n"
     */
    {
        char echo_buf[MSG_BUF_SIZE];
        int echo_len;

        echo_len = snprintf(echo_buf, sizeof(echo_buf),
                            "[SERVER] received: %s\n", msg);
        if (echo_len > 0 && echo_len < (int)sizeof(echo_buf)) {
            ssize_t sent = send(client->fd, echo_buf, echo_len, 0);
            if (sent < 0) {
                printf("[SERVER] send() echo failed to %s:%d: %s\n",
                       client->ip, client->port, strerror(errno));
            }
        }
    }

    return 0;
}

/* ===== epoll_server_run() ============================================
 *
 * 主函数：启动基于 epoll() 的多客户端聊天服务器。
 *
 * 整体流程：
 *   socket() → bind() → listen() → epoll_create1() →
 *   注册 listen_fd 到 epoll →
 *   while(1) {
 *       nfds = epoll_wait(epfd, events, MAX_EVENTS, -1)
 *       for (i = 0; i < nfds; i++) {
 *           if (events[i].data.fd == listen_fd) {
 *               accept() → add_client() → epoll_ctl(ADD)
 *           } else {
 *               recv() → 追加到缓冲区
 *               extract_messages() → 按 \n 分割
 *               for (每条完整消息) → handle_client_message()
 *               if (recv()==0 或 quit 或 出错) → remove_client() → epoll_ctl(DEL)
 *           }
 *       }
 *   }
 * ==================================================================== */
int epoll_server_run(int port)
{
    int                  listen_fd;             /* 监听套接字描述符                */
    int                  epfd;                  /* epoll 实例文件描述符             */
    int                  client_count = 0;      /* 当前已连接的客户端数量           */
    struct sockaddr_in   server_addr;           /* 服务器地址结构                  */
    struct sockaddr_in   client_addr;           /* 客户端地址结构（accept 时填充）  */
    socklen_t            client_addr_len;       /* 客户端地址长度                  */
    client_info_t        clients[MAX_CLIENTS];  /* 客户端信息数组                  */
    struct epoll_event   events[MAX_EVENTS];    /* epoll_wait 返回的事件数组       */
    int                  i;

    if (port <= 0) port = DEFAULT_PORT;

    /* 禁用 stdout 缓冲，确保日志实时输出（即使重定向到文件） */
    setbuf(stdout, NULL);

    /* 初始化客户端数组：所有槽位标记为 -1（空闲） */
    for (i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

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
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);  /* 绑定本机所有网络接口 */

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
    printf("[SERVER] listen() on port %d, waiting for connections...\n", port);
    printf("[SERVER] Max clients: %d\n", MAX_CLIENTS);

    /* ===== 步骤 4: epoll_create1() 创建 epoll 实例 ===== */
    /*
     * ===== 验收标准第 5 条：正确使用 epoll_create1() =====
     *
     * epoll_create1(0) 创建一个 epoll 实例。
     * 参数 flags=0 表示使用默认行为（LT 电平触发）。
     * 返回值 epfd 用于后续的 epoll_ctl() 和 epoll_wait() 操作。
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

    printf("\n[SERVER] Epoll chat server is running. Press Ctrl+C to stop.\n\n");

    /* ===== 步骤 5: 主事件循环 ===== */
    /*
     * epoll_wait() 对比 select()：
     *   - epoll 在内核维护感兴趣的事件列表，不需要每次重建
     *   - epoll_wait 直接返回就绪的事件数组，不需要遍历所有 fd
     *   - 时间复杂度 O(1)（就绪事件数），而 select 是 O(n)（最大 fd 数）
     */
    while (1) {
        int nfds;
        int j;

        /*
         * ===== 验收标准第 5 条：正确使用 epoll_wait() =====
         *
         * epoll_wait(epfd, events, maxevents, timeout)
         *   epfd     — epoll 实例
         *   events   — 输出：就绪的事件数组
         *   maxevents— 最多返回多少个事件
         *   timeout  — -1 表示无限期阻塞，直到有事件就绪
         *
         * 返回值：
         *   >0  — 就绪的事件数量
         *   =0  — 超时（此处不会发生，因为 timeout=-1）
         *   -1  — 出错
         */
        nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                /* 被信号中断（如 SIGINT/Ctrl+C），继续循环 */
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
                    /*
                     * 将新客户端加入管理数组。
                     * add_client() 内部会调用 epoll_ctl(ADD)
                     * 将 conn_fd 注册到 epoll。
                     */
                    if (add_client(clients, &client_count,
                                   conn_fd, &client_addr, epfd) != 0) {
                        /* 客户端列表已满，拒绝连接 */
                        close(conn_fd);
                    }
                }
            } else {
                /*
                 * ===== 情况 B：客户端 fd 可读 → 有数据到达或连接断开 =====
                 *
                 * recv() 返回值的三种情况（对应验收标准第 3、4 条）：
                 *   >0  — 读取到 n 字节，追加到缓冲区，尝试按 \n 分帧
                 *   =0  — 对方调用了 close()（发送了 FIN），连接正常关闭
                 *   <0  — 出错（errno 可判断具体错误类型）
                 */
                /* 找到对应的客户端槽位 */
                int client_idx = -1;
                for (i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == ready_fd) {
                        client_idx = i;
                        break;
                    }
                }
                if (client_idx == -1) {
                    /* 找不到对应的客户端（可能已经被清理），忽略 */
                    continue;
                }

                {
                    int client_fd = clients[client_idx].fd;
                    char tmp_buf[RECV_BUF_SIZE];
                    ssize_t n;

                    memset(tmp_buf, 0, sizeof(tmp_buf));
                    n = recv(client_fd, tmp_buf, sizeof(tmp_buf) - 1, 0);

                    if (n < 0) {
                        /* ===== 验收标准第 4 条：recv 出错处理 ===== */
                        printf("[SERVER] recv() error on fd=%d: %s\n",
                               client_fd, strerror(errno));
                        remove_client(clients, client_idx, &client_count,
                                      epfd, "recv error");
                        continue;
                    }

                    if (n == 0) {
                        /*
                         * ===== 验收标准第 4 条：recv()==0 → 客户端断开连接 =====
                         *
                         * TCP 连接：对方调用了 close() 或进程终止，
                         * 内核发送 FIN 段。recv() 返回 0 表示收到 FIN，
                         * 即对方已经关闭了连接。
                         */
                        printf("[SERVER] recv() returned 0 on fd=%d — client closed connection\n",
                               client_fd);
                        remove_client(clients, client_idx, &client_count,
                                      epfd, "client closed (recv==0)");
                        continue;
                    }

                    /* recv() > 0：收到数据，追加到客户端接收缓冲区 */
                    printf("[SERVER] recv() %zd bytes from %s:%d (fd=%d)\n",
                           n, clients[client_idx].ip,
                           clients[client_idx].port, client_fd);

                    /*
                     * ===== 验收标准第 3 条：消息分帧 =====
                     *
                     * 将新数据追加到缓冲区，然后按 \n 提取完整消息。
                     * 不把一次 recv() 当成一条消息！
                     */
                    {
                        int remaining = RECV_BUF_SIZE - clients[client_idx].buf_len;
                        if (n > remaining) {
                            /* 缓冲区溢出保护：截断 */
                            n = remaining;
                        }
                        memcpy(clients[client_idx].recv_buf + clients[client_idx].buf_len,
                               tmp_buf, n);
                        clients[client_idx].buf_len += n;
                        clients[client_idx].recv_buf[clients[client_idx].buf_len] = '\0';
                    }

                    /*
                     * 提取所有完整消息（以 \n 为分隔符）
                     *
                     * extract_messages 处理：
                     *   - 半包：buf 中还没有 \n → msg_count=0，等下次 recv
                     *   - 粘包：一次收到多条消息 → msg_count>1，逐条处理
                     */
                    {
                        char *msg_list[128];  /* 单次最多提取 128 条消息 */
                        int msg_count;
                        int m;

                        msg_count = extract_messages(&clients[client_idx],
                                                     msg_list, 128);

                        for (m = 0; m < msg_count; m++) {
                            int should_quit;

                            should_quit = handle_client_message(&clients[client_idx],
                                                               msg_list[m]);
                            if (should_quit) {
                                /*
                                 * ===== 验收标准第 4 条：quit\n 退出 =====
                                 *
                                 * 客户端发送 quit\n，服务器主动关闭该客户端连接。
                                 */
                                remove_client(clients, client_idx, &client_count,
                                              epfd, "quit command");
                                break;  /* 不再处理该客户端的后续消息 */
                            }
                        }
                    }
                }
            }
        }
    }

    /* ===== 清理：关闭所有客户端和监听套接字 ===== */
    printf("\n[SERVER] Shutting down...\n");
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
    printf("[SERVER] Server stopped.\n");

    return 0;
}
