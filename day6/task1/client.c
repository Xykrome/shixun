/*
 * W2D1 训练1：基于TCP的文本聊天程序 —— 客户端
 *
 * 功能：
 *   1. 以命令行方式启动，参数为：服务器IP地址 和 端口号
 *   2. 创建TCP套接字，连接到服务器
 *   3. 连接成功后与服务器进行文本通信
 *   4. 输入 "exit" 结束与服务器的通信并退出程序
 *
 * 使用方法：
 *   ./client <服务器IP> <端口号>
 *   例如：./client 127.0.0.1 8080
 *
 * 覆盖知识点：
 *   - socket()   创建套接字
 *   - connect()  建立与服务器的TCP连接（三次握手）
 *   - send()     发送数据
 *   - recv()     接收数据
 *   - close()    关闭套接字
 *   - sockaddr_in 地址结构 / inet_addr / 字符串转IP
 *   - gethostbyname() 或 inet_addr() 地址转换
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE   1024          /* 缓冲区大小 */

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

    /* ===== 步骤1: 调用 socket() 创建套接字 ===== */
    /*
     * socket(family, type, protocol)
     *   AF_INET      — IPv4 协议族
     *   SOCK_STREAM  — 字节流套接字（TCP）
     *   0            — 自动选择协议
     */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket() 创建套接字失败");
        exit(EXIT_FAILURE);
    }
    printf("[客户端] 套接字创建成功，fd = %d\n", sock_fd);

    /* ===== 步骤2: 调用 connect() 连接服务器 ===== */
    /*
     * connect(sockfd, servaddr, addrlen)
     * 对于字节流套接字，connect会发起TCP三次握手建立连接
     * 需要填充服务器端的 sockaddr_in 结构：
     *   sin_family — AF_INET
     *   sin_port   — 端口号（网络字节序）
     *   sin_addr   — IP地址（网络字节序）
     *
     * inet_addr() 将 点分十进制IP字符串 → 32位网络字节序IP地址
     */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

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
    printf("[客户端] 连接成功！开始聊天（输入 'exit' 退出）\n\n");

    /* ===== 步骤3: 与服务器进行文本通信 ===== */
    while (1) {
        /* --- 3a: 用户输入消息 --- */
        printf("[客户端(你)] ");
        fflush(stdout);

        if (fgets(buf, BUFSIZE, stdin) == NULL) {
            break;
        }

        /* 去掉末尾换行符 */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
            len--;
        }
        if (len == 0) {
            continue;                          /* 空行则跳过 */
        }

        /* --- 3b: 发送消息给服务器 --- */
        /* send(sockfd, data, len, flags) — 在TCP连接上发送数据 */
        if (send(sock_fd, buf, len, 0) < 0) {
            perror("send() 发送失败");
            break;
        }

        /* 如果输入了 "exit"，退出 */
        if (strcmp(buf, "exit") == 0) {
            printf("[客户端] 发送 exit，退出程序\n");
            break;
        }

        /* --- 3c: 接收服务器的回复 --- */
        memset(buf, 0, BUFSIZE);

        /*
         * recv(sockfd, buf, len, flags)
         * 从TCP连接接收数据，无数据时阻塞等待
         * 返回实际接收字节数，连接关闭返回0
         */
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
        printf("[服务器] %s\n", buf);

        /* 如果服务器发来 "exit"，退出 */
        if (strncmp(buf, "exit", 4) == 0 && (n == 4 || buf[4] == '\n' || buf[4] == '\r')) {
            printf("[客户端] 服务器已退出\n");
            break;
        }
    }

    /* ===== 步骤4: 关闭连接 ===== */
    /*
     * close(sockfd)
     * 关闭套接字，发送FIN段，断开TCP连接
     */
    close(sock_fd);
    printf("[客户端] 套接字已关闭，程序结束\n");

    return 0;
}
