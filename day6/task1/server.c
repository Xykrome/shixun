/*
 * W2D1 训练1：基于TCP的文本聊天程序 —— 服务器端
 *
 * 功能：
 *   1. 创建TCP套接字，绑定端口，进入监听状态
 *   2. 接受客户端连接后，与客户端进行文本通信
 *   3. 收到客户端发来的 "exit" 后退出程序
 *
 * 覆盖知识点：
 *   - socket()  创建套接字
 *   - bind()    绑定本地地址和端口
 *   - listen()  将主动套接字转为被动套接字
 *   - accept()  接受客户端连接
 *   - recv()    接收数据
 *   - send()    发送数据
 *   - close()   关闭套接字
 *   - sockaddr_in 地址结构 / htons / inet_addr / 网络字节序
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT      8080          /* 服务器监听端口 */
#define BUFSIZE   1024          /* 缓冲区大小 */
#define BACKLOG   5             /* 最大排队连接数 */

int main(void)
{
    int listen_fd, conn_fd;                 /* listen_fd: 监听套接字, conn_fd: 连接套接字 */
    struct sockaddr_in server_addr;          /* 服务器地址结构 */
    struct sockaddr_in client_addr;          /* 客户端地址结构 */
    socklen_t client_addr_len;               /* 客户端地址长度 */
    char buf[BUFSIZE];                       /* 收发缓冲区 */
    ssize_t n;

    /* ===== 步骤1: 调用 socket() 创建套接字 ===== */
    /*
     * socket(family, type, protocol)
     *   AF_INET      — IPv4 协议族
     *   SOCK_STREAM  — 字节流套接字（基于TCP，可靠、有序）
     *   0            — 协议自动选择（SOCK_STREAM → TCP）
     * 返回值：非负文件描述符（成功），-1（失败）
     */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket() 创建套接字失败");
        exit(EXIT_FAILURE);
    }
    printf("[服务器] 套接字创建成功，fd = %d\n", listen_fd);

    /* ===== 步骤2: 调用 bind() 绑定本地地址和端口 ===== */
    /*
     * bind(sockfd, addr, addrlen)
     * 将套接字与本地IP地址和端口号绑定
     * 结构体 sockaddr_in 需要包含：
     *   sin_family — 协议族（AF_INET）
     *   sin_port   — 端口号（网络字节序，使用 htons() 转换）
     *   sin_addr   — IP地址（网络字节序，INADDR_ANY 表示绑定所有网卡）
     */
    memset(&server_addr, 0, sizeof(server_addr));             /* 先清零 */
    server_addr.sin_family      = AF_INET;                     /* IPv4 */
    server_addr.sin_port        = htons(PORT);                 /* 主机字节序→网络字节序 */
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);           /* 绑定所有本地IP */

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind() 绑定失败");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    printf("[服务器] 绑定端口 %d 成功\n", PORT);

    /* ===== 步骤3: 调用 listen() 进入监听状态 ===== */
    /*
     * listen(sockfd, backlog)
     * 将主动套接字转为被动套接字，开始等待客户端连接
     * backlog — 已完成连接队列的最大长度
     */
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen() 监听失败");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    printf("[服务器] 开始监听，等待客户端连接...\n");

    /* ===== 步骤4: 调用 accept() 接受客户端连接 ===== */
    /*
     * accept(sockfd, cliaddr, addrlen)
     * 从已完成连接队列取出一个连接，返回新的套接字描述符用于通信
     * 如果队列为空，则进程进入睡眠等待
     * 对客户端信息不关心时可传 NULL
     */
    client_addr_len = sizeof(client_addr);
    conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (conn_fd < 0) {
        perror("accept() 接受连接失败");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    printf("[服务器] 客户端 %s:%d 已连接，fd = %d\n",
           inet_ntoa(client_addr.sin_addr),       /* 网络字节序IP → 点分十进制字符串 */
           ntohs(client_addr.sin_port),            /* 网络字节序端口 → 主机字节序 */
           conn_fd);

    /* ===== 步骤5: 与客户端进行文本通信 ===== */
    printf("[服务器] 开始聊天，等待客户端消息...（收到 'exit' 则退出）\n\n");

    while (1) {
        memset(buf, 0, BUFSIZE);

        /* --- 5a: 接收客户端消息 --- */
        /*
         * recv(sockfd, buf, len, flags)
         * 从TCP连接接收数据
         * 返回实际接收的字节数，连接关闭返回0，出错返回-1
         * 无数据可读时进程将阻塞等待
         */
        n = recv(conn_fd, buf, BUFSIZE - 1, 0);
        if (n <= 0) {
            if (n == 0) {
                printf("[服务器] 客户端已断开连接\n");
            } else {
                perror("recv() 接收失败");
            }
            break;
        }
        buf[n] = '\0';
        printf("[客户端] %s\n", buf);

        /* 收到 "exit" 则退出 */
        if (strncmp(buf, "exit", 4) == 0 && (n == 4 || buf[4] == '\n' || buf[4] == '\r')) {
            printf("[服务器] 收到 exit，退出程序\n");
            break;
        }

        /* --- 5b: 服务器回复消息 --- */
        printf("[服务器(你)] ");
        fflush(stdout);

        if (fgets(buf, BUFSIZE, stdin) == NULL) {
            break;
        }

        /* 去掉末尾换行符以便发送 */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
            len--;
        }

        /* 发送给客户端 */
        /* send(sockfd, data, len, flags) — 在TCP连接上发送数据 */
        if (send(conn_fd, buf, len, 0) < 0) {
            perror("send() 发送失败");
            break;
        }

        /* 如果服务器也输入了 exit，则退出（可选） */
        if (strcmp(buf, "exit") == 0) {
            printf("[服务器] 发送 exit，退出程序\n");
            break;
        }
    }

    /* ===== 步骤6: 关闭套接字，释放资源 ===== */
    /*
     * close(sockfd)
     * 关闭套接字描述符。TCP协议会继续将未发送数据传完，然后发送FIN段
     */
    close(conn_fd);      /* 先关闭连接套接字 */
    close(listen_fd);    /* 再关闭监听套接字 */
    printf("[服务器] 套接字已关闭，程序结束\n");

    return 0;
}
