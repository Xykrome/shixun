/*
 * W2D4 select_server.c — 基于 select() 的多客户端聊天服务器
 *
 * 功能：
 *   1. socket()   创建 TCP 监听套接字
 *   2. bind()     绑定 host:port
 *   3. listen()   进入监听状态
 *   4. select()   I/O 多路复用，同时监听 listen_fd 和所有 client_fd
 *   5. accept()   接受新客户端连接（非阻塞语义，select 保证可读）
 *   6. recv()     接收客户端消息（按 \n 分帧，处理粘包/半包）
 *   7. send()     将消息回显给对应客户端
 *   8. close()    关闭客户端连接
 *
 * 对照 W2D4 知识点：
 *   - select() 的工作机制：轮询文件描述符，返回就绪的 fd 数量
 *   - fd_set 的操作：FD_ZERO(清空) / FD_SET(加入) / FD_ISSET(检测) / FD_CLR(移除)
 *   - select 第一个参数 nfds = max_fd + 1 的含义
 *   - 为什么 select 每次循环前要重新设置 fd_set（select 会修改传入的集合）
 *   - 基于 \n 的消息分帧：TCP 是字节流，一次 recv() 不等于一条消息
 *   - 客户端断开 vs 主动退出的处理差异（recv()==0 vs quit\n）
 */

#include "select_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
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
 *
 * 返回值：
 *    0  - 成功添加
 *   -1  - 客户端列表已满
 */
static int add_client(client_info_t *clients, int *client_count,
                      int conn_fd, struct sockaddr_in *client_addr)
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
 * 关键操作（对应验收标准第 7、8、9 条）：
 *   1. FD_CLR(client_fd, &master_set)  — 从 select 监听集合中移除
 *   2. close(client_fd)                — 关闭套接字
 *   3. clients[i].fd = -1              — 标记槽位为空闲
 *   4. client_count--                  — 连接数减一
 *
 * 参数：
 *   i            - 客户端在数组中的索引
 *   reason       - 断开原因（用于日志输出）
 */
static void remove_client(client_info_t *clients, int i, int *client_count,
                          fd_set *master_set, const char *reason)
{
    int fd = clients[i].fd;

    printf("[SERVER] Client %s:%d disconnected (fd=%d) — %s\n",
           clients[i].ip, clients[i].port, fd, reason);

    /* ===== 验收标准第 9 条：正确使用 FD_CLR ===== */
    FD_CLR(fd, master_set);   /* 从 select 监听集合中移除该 fd   */

    close(fd);                /* 关闭套接字，释放内核资源          */

    clients[i].fd = -1;       /* 标记槽位为空闲，供新连接复用      */
    (*client_count)--;         /* 连接计数减一                      */

    printf("[SERVER] Active connections: %d\n", *client_count);
}

/*
 * 处理来自客户端的消息
 *
 * 检查是否是 quit\n（验收标准第 7 条），
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
    /* ===== 验收标准第 7 条：quit\n 退出 ===== */
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

/* ===== select_server_run() ============================================
 *
 * 主函数：启动基于 select() 的多客户端聊天服务器。
 *
 * 整体流程：
 *   socket() → bind() → listen() → 初始化 fd_set →
 *   while(1) {
 *       FD_ZERO + FD_SET 重建监听集合
 *       select(max_fd+1, &read_fds, NULL, NULL, NULL)
 *       if (listen_fd 可读) → accept() → 加入客户端列表
 *       for (每个客户端) {
 *           if (客户端 fd 可读) {
 *               recv() → 追加到缓冲区
 *               extract_messages() → 按 \n 分割
 *               for (每条完整消息) → handle_client_message()
 *               if (recv()==0 或 quit 或 出错) → remove_client()
 *           }
 *       }
 *   }
 * ==================================================================== */
int select_server_run(int port)
{
    int                  listen_fd;             /* 监听套接字描述符                */
    int                  max_fd;                /* 当前最大的 fd 值（select nfds）  */
    int                  client_count = 0;      /* 当前已连接的客户端数量           */
    struct sockaddr_in   server_addr;           /* 服务器地址结构                  */
    struct sockaddr_in   client_addr;           /* 客户端地址结构（accept 时填充）  */
    socklen_t            client_addr_len;       /* 客户端地址长度                  */
    client_info_t        clients[MAX_CLIENTS];  /* 客户端信息数组                  */
    fd_set               master_set;            /* 主监听集合（包含所有感兴趣的 fd）*/
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
    printf("\n[SERVER] Chat server is running. Press Ctrl+C to stop.\n\n");

    /* ===== 步骤 4: 初始化 fd_set 并进入主循环 ===== */
    /*
     * master_set 维护所有需要 select 监听的 fd：
     *   - listen_fd：检测新连接
     *   - 各 client_fd：检测客户端消息
     *
     * 注意：select() 会修改传入的 fd_set（将未就绪的 fd 清除），
     * 因此每次循环都需要用 master_set 重新初始化 read_fds。
     */
    FD_ZERO(&master_set);
    FD_SET(listen_fd, &master_set);    /* 将 listen_fd 加入监听集合 */
    max_fd = listen_fd;                /* 初始时只有 listen_fd      */

    while (1) {
        fd_set read_fds;               /* 本轮 select 的可读集合    */
        int    activity;               /* select 返回的就绪 fd 数量 */

        /*
         * ===== 验收标准第 9 条：正确使用 FD_ZERO 和 FD_SET =====
         *
         * 每次 select 调用前必须重新设置 read_fds，
         * 因为 select 返回时会修改它（只保留就绪的 fd）。
         */
        FD_ZERO(&read_fds);                        /* 清空集合        */
        read_fds = master_set;                     /* 复制主集合      */

        /*
         * ===== 验收标准第 9 条：正确使用 select() =====
         *
         * select(nfds, readfds, writefds, exceptfds, timeout)
         *   nfds  = max_fd + 1（内核从 0 检查到 nfds-1）
         *   NULL  = 不关心写就绪
         *   NULL  = 不关心异常
         *   NULL  = 无限期阻塞，直到有 fd 就绪
         *
         * 返回值：
         *   >0  — 就绪的 fd 数量
         *   =0  — 超时（此处不会发生，因为 timeout=NULL）
         *   -1  — 出错
         */
        activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR) {
                /* 被信号中断（如 SIGINT/Ctrl+C），继续循环 */
                continue;
            }
            perror("[SERVER] select() failed");
            break;
        }

        /*
         * ===== 情况 A：listen_fd 可读 → 有新客户端连接 =====
         *
         * FD_ISSET 检查 listen_fd 是否在 read_fds 中。
         *
         * select 保证 accept() 不会阻塞，因为 listen_fd
         * 可读意味着已完成连接队列非空。
         */
        if (FD_ISSET(listen_fd, &read_fds)) {
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
                 * 如果成功，还需要将 conn_fd 加入 master_set
                 * 并更新 max_fd。
                 */
                if (add_client(clients, &client_count,
                               conn_fd, &client_addr) == 0) {

                    /* ===== 验收标准第 9 条：FD_SET ===== */
                    FD_SET(conn_fd, &master_set);

                    /* 更新 max_fd（select 的 nfds = max_fd + 1） */
                    if (conn_fd > max_fd) {
                        max_fd = conn_fd;
                    }
                } else {
                    /* 客户端列表已满，拒绝连接 */
                    close(conn_fd);
                }
            }
        }

        /*
         * ===== 情况 B：客户端 fd 可读 → 有数据到达或连接断开 =====
         *
         * 遍历所有客户端，检查其 fd 是否在 read_fds 中。
         * 如果是，调用 recv() 读取数据。
         *
         * recv() 返回值的三种情况（验收标准第 6、7、8 条）：
         *   >0  — 读取到 n 字节，追加到缓冲区，尝试按 \n 分帧
         *   =0  — 对方调用了 close()（发送了 FIN），连接正常关闭
         *   <0  — 出错（errno 可判断具体错误类型）
         */
        for (i = 0; i < MAX_CLIENTS; i++) {
            int client_fd = clients[i].fd;

            if (client_fd == -1) continue;  /* 空闲槽位，跳过 */

            /* ===== 验收标准第 9 条：FD_ISSET ===== */
            if (FD_ISSET(client_fd, &read_fds)) {
                char tmp_buf[RECV_BUF_SIZE];
                ssize_t n;

                memset(tmp_buf, 0, sizeof(tmp_buf));
                n = recv(client_fd, tmp_buf, sizeof(tmp_buf) - 1, 0);

                if (n < 0) {
                    /* ===== 验收标准第 8 条：recv 出错处理 ===== */
                    printf("[SERVER] recv() error on fd=%d: %s\n",
                           client_fd, strerror(errno));
                    remove_client(clients, i, &client_count,
                                  &master_set, "recv error");
                    continue;
                }

                if (n == 0) {
                    /*
                     * ===== 验收标准第 8 条：recv()==0 → 客户端断开连接 =====
                     *
                     * TCP 连接：对方调用了 close() 或进程终止，
                     * 内核发送 FIN 段。recv() 返回 0 表示收到 FIN，
                     * 即对方已经关闭了连接。
                     *
                     * 处理：FD_CLR + close（见 remove_client 函数）
                     */
                    printf("[SERVER] recv() returned 0 on fd=%d — client closed connection\n",
                           client_fd);
                    remove_client(clients, i, &client_count,
                                  &master_set, "client closed (recv==0)");
                    continue;
                }

                /* recv() > 0：收到数据，追加到客户端接收缓冲区 */
                printf("[SERVER] recv() %zd bytes from %s:%d (fd=%d)\n",
                       n, clients[i].ip, clients[i].port, client_fd);

                /*
                 * ===== 验收标准第 6 条：消息分帧 =====
                 *
                 * 将新数据追加到缓冲区，然后按 \n 提取完整消息。
                 * 不把一次 recv() 当成一条消息！
                 */
                {
                    int remaining = RECV_BUF_SIZE - clients[i].buf_len;
                    if (n > remaining) {
                        /* 缓冲区溢出保护：截断 */
                        n = remaining;
                    }
                    memcpy(clients[i].recv_buf + clients[i].buf_len,
                           tmp_buf, n);
                    clients[i].buf_len += n;
                    clients[i].recv_buf[clients[i].buf_len] = '\0';
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

                    msg_count = extract_messages(&clients[i], msg_list, 128);

                    for (m = 0; m < msg_count; m++) {
                        int should_quit;

                        should_quit = handle_client_message(&clients[i],
                                                            msg_list[m]);
                        if (should_quit) {
                            /*
                             * ===== 验收标准第 7 条：quit\n 退出 =====
                             *
                             * 客户端发送 quit\n，服务器主动关闭该客户端连接。
                             */
                            remove_client(clients, i, &client_count,
                                          &master_set, "quit command");
                            break;  /* 不再处理该客户端的后续消息 */
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
            close(clients[i].fd);
            clients[i].fd = -1;
        }
    }
    close(listen_fd);
    printf("[SERVER] Server stopped.\n");

    return 0;
}
