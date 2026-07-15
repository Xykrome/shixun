/*
 * W2D4 chat_client.c — 聊天客户端
 *
 * 功能：
 *   1. socket()    创建 TCP 套接字
 *   2. connect()   连接到聊天服务器
 *   3. select()    同时监听 stdin（用户输入）和服务器 socket
 *   4. 从 stdin 读取用户输入，发送到服务器（自动追加 \n）
 *   5. 从服务器接收回显消息并打印
 *   6. 发送 quit\n → 通知服务器 → 退出
 *
 * 使用方法：
 *   ./chat_client <server_ip> <server_port>
 *
 * 示例：
 *   ./chat_client 127.0.0.1 8888
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 4096

int main(int argc, char *argv[])
{
    const char        *server_ip;
    int                server_port;
    int                sock_fd;
    struct sockaddr_in server_addr;
    int                ret;

    /* 解析命令行参数 */
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        fprintf(stderr, "Example: %s 127.0.0.1 8888\n", argv[0]);
        return 1;
    }
    server_ip   = argv[1];
    server_port = atoi(argv[2]);
    if (server_port <= 0 || server_port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return 1;
    }

    /* ===== 步骤 1: socket() 创建 TCP 套接字 ===== */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket() failed");
        return 1;
    }

    /* ===== 步骤 2: connect() 连接到服务器 ===== */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect() failed");
        close(sock_fd);
        return 1;
    }
    printf("[CLIENT] Connected to %s:%d\n", server_ip, server_port);
    printf("[CLIENT] Type messages and press Enter to send.\n");
    printf("[CLIENT] Type 'quit' and press Enter to exit.\n\n");

    /* ===== 步骤 3: select() 同时监听 stdin 和服务器 ===== */
    /*
     * stdin 的文件描述符是 STDIN_FILENO (0)。
     * select 可以同时监听普通文件描述符和套接字文件描述符。
     *
     * 两种就绪情况：
     *   stdin 可读 → 用户输入了内容 → send() 到服务器
     *   sock_fd 可读 → 服务器发来了数据 → recv() 并打印
     */
    while (1) {
        fd_set read_fds;
        int    max_fd;

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);   /* 监听标准输入（用户输入） */
        FD_SET(sock_fd, &read_fds);        /* 监听服务器 socket       */

        /* max_fd = 两者中较大的那个 */
        max_fd = (STDIN_FILENO > sock_fd) ? STDIN_FILENO : sock_fd;

        ret = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("select() failed");
            break;
        }

        /* ===== 情况 A: stdin 可读 → 用户输入了内容 ===== */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char input[BUF_SIZE];

            if (fgets(input, sizeof(input), stdin) == NULL) {
                /* stdin EOF（Ctrl+D），退出 */
                printf("\n[CLIENT] EOF on stdin, exiting.\n");
                break;
            }

            /* 去掉末尾的换行符（fgets 会保留 \n） */
            /* send 时不会自动追加 \n，消息分帧由服务器处理 */
            /* 不过这里保留 \n，因为服务器按 \n 分帧 */

            /* 发送到服务器 */
            ssize_t sent = send(sock_fd, input, strlen(input), 0);
            if (sent < 0) {
                perror("send() failed");
                break;
            }
            printf("[CLIENT] Sent %zd bytes\n", sent);

            /* 检查是否是 quit 命令（不含 \n 比较） */
            {
                size_t len = strlen(input);
                /* 去掉末尾的 \n 和 \r */
                while (len > 0 && (input[len-1] == '\n' || input[len-1] == '\r')) {
                    input[len-1] = '\0';
                    len--;
                }
                if (strcmp(input, "quit") == 0) {
                    printf("[CLIENT] Sent quit, waiting for server to close...\n");
                    /*
                     * 不立即退出，等待服务器关闭连接。
                     * 当服务器 close(fd) 后，sock_fd 会变为可读，
                     * recv() 将返回 0（FIN），客户端再退出。
                     */
                }
            }
        }

        /* ===== 情况 B: sock_fd 可读 → 服务器发来了数据 ===== */
        if (FD_ISSET(sock_fd, &read_fds)) {
            char recv_buf[BUF_SIZE];
            ssize_t n;

            memset(recv_buf, 0, sizeof(recv_buf));
            n = recv(sock_fd, recv_buf, sizeof(recv_buf) - 1, 0);

            if (n < 0) {
                perror("recv() failed");
                break;
            }

            if (n == 0) {
                /*
                 * 服务器关闭了连接（recv() == 0）
                 * 可能原因：
                 *   1. 我们发送了 quit，服务器主动 close
                 *   2. 服务器关闭
                 */
                printf("[CLIENT] Server closed the connection.\n");
                break;
            }

            /* 显示服务器返回的内容 */
            recv_buf[n] = '\0';
            printf("[SERVER→] %s", recv_buf);
            /* 如果末尾没有 \n，补充一个换行 */
            if (n > 0 && recv_buf[n-1] != '\n') {
                printf("\n");
            }
        }
    }

    /* ===== 清理 ===== */
    close(sock_fd);
    printf("[CLIENT] Disconnected. Goodbye.\n");
    return 0;
}
