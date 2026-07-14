/*
 * W2D2 训练1：面向多客户连接的TCP文本聊天程序 —— 客户端
 *
 * 功能：
 *   1. 以命令行方式启动，参数为：服务器IP地址 和 端口号
 *   2. 创建TCP套接字，连接到服务器（connect → 三次握手）
 *   3. 连接成功后与服务器进行文本通信（send/recv）
 *   4. 输入 "exit" 结束与服务器的通信并退出程序
 *
 * 使用方法：
 *   ./client <服务器IP> <端口号>
 *   例如：./client 127.0.0.1 8080
 *
 * 覆盖知识点（W2D1+W2D2）：
 *   - socket()    创建套接字
 *   - connect()   建立与服务器的TCP连接（发起三次握手）
 *   - send()      在TCP连接上发送数据
 *   - recv()      从TCP连接接收数据
 *   - close()     关闭套接字（发起四次挥手）
 *   - sockaddr_in 地址结构 / inet_addr() / htons() 网络字节序
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE   1024          /* 收发缓冲区大小 */

int main(int argc, char *argv[])
{
    int sock_fd;                             /* 客户端套接字描述符 */
    struct sockaddr_in server_addr;          /* 服务器地址结构 */
    char buf[BUFSIZE];                       /* 收发缓冲区 */
    ssize_t n;

    /* ===== 参数检查 ===== */
    if (argc != 3) {
        fprintf(stderr, "用法: %s <服务器IP地址> <端口号>\n", argv[0]);
        fprintf(stderr, "示例: %s 127.0.0.1 8080\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip   = argv[1];        /* 服务器IP（点分十进制） */
    int         server_port = atoi(argv[2]);  /* 服务器端口号 */

    /* ===== 步骤1: socket() 创建套接字 ===== */
    /*
     * socket(family, type, protocol)
     *   AF_INET      — IPv4 协议族
     *   SOCK_STREAM  — 字节流套接字（TCP），可靠、有序、双向
     *   0            — 自动选择协议（→ IPPROTO_TCP）
     */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket() 创建套接字失败");
        exit(EXIT_FAILURE);
    }
    printf("[客户端] 套接字创建成功，fd = %d\n", sock_fd);

    /* ===== 步骤2: connect() 建立与服务器的TCP连接 ===== */
    /*
     * connect(sockfd, servaddr, addrlen)
     *
     * 对于 SOCK_STREAM（TCP）套接字，connect 执行三次握手：
     *   客户端 → 服务器: SYN (seq=x)
     *   服务器 → 客户端: SYN+ACK (seq=y, ack=x+1)
     *   客户端 → 服务器: ACK (ack=y+1)
     *
     * 三次握手完成后 connect 返回，此时连接建立成功。
     *
     * 需要填充 sockaddr_in 结构体：
     *   sin_family = AF_INET              — IPv4 协议族
     *   sin_port   = htons(port)          — 端口号，主机字节序→网络字节序
     *   sin_addr   = inet_addr(IP字符串)  — 点分十进制→32位网络字节序
     */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    /* 地址格式校验：inet_addr() 无效时返回 INADDR_NONE */
    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "无效的IP地址: %s\n", server_ip);
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("[客户端] 正在连接 %s:%d ...\n", server_ip, server_port);
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect() 连接服务器失败");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    printf("[客户端] 连接成功！\n\n");

    /*
     * 先接收服务器的欢迎消息（由子进程发送）
     * 注意：此处使用 recv 而非 read，recv 专为套接字设计
     */
    memset(buf, 0, BUFSIZE);
    n = recv(sock_fd, buf, BUFSIZE - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    /* ===== 步骤3: 与服务器进行文本通信 ===== */
    while (1) {
        /* ---- 3a: 用户输入消息 ---- */
        printf("[客户端(你)] ");
        fflush(stdout);

        if (fgets(buf, BUFSIZE, stdin) == NULL) {
            /* EOF (Ctrl+D) → 结束程序 */
            printf("\n");
            break;
        }

        /* 去掉末尾换行符 */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
            len--;
        }
        if (len == 0) {
            continue;                          /* 空行跳过 */
        }

        /* ---- 3b: 发送消息给服务器 ---- */
        /*
         * send(sockfd, data, len, flags)
         * 在已连接的TCP套接字上发送数据。
         * TCP 保证数据按序、可靠地送达对端。
         */
        if (send(sock_fd, buf, len, 0) < 0) {
            perror("send() 发送失败");
            break;
        }

        /* 如果输入了 "exit"，通知服务器后退出 */
        if (strcmp(buf, "exit") == 0) {
            printf("[客户端] 发送 exit，退出程序\n");

            /* 接收服务器的告别消息 */
            memset(buf, 0, BUFSIZE);
            n = recv(sock_fd, buf, BUFSIZE - 1, 0);
            if (n > 0) {
                buf[n] = '\0';
                printf("%s", buf);
            }
            break;
        }

        /* ---- 3c: 接收服务器的回复 ---- */
        /*
         * recv(sockfd, buf, len, flags)
         * 从TCP连接接收数据，无数据到达时进程将阻塞等待。
         * 返回 0 表示对端关闭连接（收到 FIN），返回 -1 表示出错。
         */
        memset(buf, 0, BUFSIZE);
        n = recv(sock_fd, buf, BUFSIZE - 1, 0);
        if (n <= 0) {
            if (n == 0) {
                printf("[客户端] 服务器已断开连接\n");
            } else {
                perror("recv() 接收失败");
            }
            break;
        }
        buf[n] = '\0';
        printf("%s\n", buf);
    }

    /* ===== 步骤4: close() 关闭连接 ===== */
    /*
     * close(sockfd)
     * 关闭套接字，内核发送 FIN 段，执行 TCP 四次挥手：
     *   客户端 → 服务器: FIN (seq=u)
     *   服务器 → 客户端: ACK (ack=u+1)
     *   服务器 → 客户端: FIN (seq=v)
     *   客户端 → 服务器: ACK (ack=v+1)
     * 此后客户端进入 TIME_WAIT 状态（约 2MSL = 60秒）
     */
    close(sock_fd);
    printf("[客户端] 套接字已关闭，程序结束\n");

    return 0;
}
