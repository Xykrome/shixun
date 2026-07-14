/*
 * V0.6 tcp_server.c — TCP 网络服务器模块
 *
 * 功能：
 *   1. socket()   创建 TCP 套接字
 *   2. bind()     绑定 host:port（从 conf/server.conf 读取）
 *   3. listen()   将主动套接字转为被动套接字，等待连接
 *   4. accept()   接受一个客户端连接，返回新的连接套接字
 *   5. recv()     接收客户端发来的 HTTP 请求报文
 *   6. 调用 handle_request_string() 处理请求，生成 HTTP 响应
 *   7. send()     将 HTTP 响应发送回客户端
 *   8. close()    关闭套接字，退出
 *
 * 对照 W2D1 知识点：
 *   - socket() / bind() / listen() / accept() / connect() 的工作流程
 *   - sockaddr_in 地址结构的使用
 *   - htons() / inet_addr() 网络字节序转换
 *   - recv() / send() 数据收发
 *   - 流式套接字（SOCK_STREAM）基于 TCP 的可靠传输
 */

#include "tcp_server.h"
#include "request_handler.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG      5          /* 已完成连接队列最大长度  */
#define RECV_BUF_SIZE 8192      /* HTTP 请求接收缓冲区大小  */
#define RESP_BUF_SIZE 8192      /* HTTP 响应发送缓冲区大小  */

int tcp_server_run(const server_config_t *config)
{
    int listen_fd;                       /* 监听套接字描述符       */
    int conn_fd;                         /* 连接套接字描述符       */
    struct sockaddr_in server_addr;      /* 服务器地址结构         */
    struct sockaddr_in client_addr;      /* 客户端地址结构         */
    socklen_t client_addr_len;           /* 客户端地址长度         */
    char recv_buf[RECV_BUF_SIZE];        /* 接收缓冲区             */
    char resp_buf[RESP_BUF_SIZE];        /* 响应缓冲区             */
    ssize_t n;
    char log_msg[512];

    /* ===== 步骤 1: socket() 创建套接字 =====
     *
     * socket(family, type, protocol)
     *   AF_INET      — IPv4 协议族
     *   SOCK_STREAM  — 字节流套接字（TCP 协议，双向、可靠、有序）
     *   0            — 根据 type 自动选择协议（SOCK_STREAM → IPPROTO_TCP）
     *
     * 返回值：非负整数 = 套接字文件描述符，-1 = 失败
     */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        log_error("socket() failed");
        perror("socket");
        return -1;
    }
    snprintf(log_msg, sizeof(log_msg),
             "socket() created, fd=%d", listen_fd);
    log_info(log_msg);

    /* SO_REUSEADDR：允许服务器重启时立即重用端口
     *
     * TCP 连接关闭后端口会进入 TIME_WAIT 状态（默认约 60 秒），
     * 期间再次 bind 同一端口会失败（"Address already in use"）。
     *
     * setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))
     * 设置后即可在 TIME_WAIT 期间重新 bind 该端口，服务器重启无需等待。
     */
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

    /* ===== 步骤 2: bind() 绑定本地地址和端口 =====
     *
     * bind(sockfd, addr, addrlen)
     * 将套接字与指定的 IP 地址和端口号绑定。
     * 服务器必须 bind 一个众所周知的端口，客户端才能 connect 到它。
     *
     * sockaddr_in 结构体：
     *   sin_family = AF_INET       — IPv4 协议族
     *   sin_port   = htons(port)   — 端口号，主机字节序→网络字节序（大端）
     *   sin_addr   = inet_addr()   — IP 地址，点分十进制→32位网络字节序
     *
     * INADDR_ANY (htonl) 表示绑定本机所有可用的网络接口
     */
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

    /* ===== 步骤 3: listen() 进入监听状态 =====
     *
     * listen(sockfd, backlog)
     * 将 socket() 创建的主动套接字转换为被动套接字，
     * 告诉内核接受指向此套接字的连接请求。
     *
     * backlog 参数指定内核为此套接字排队的最大连接个数：
     *   未完成连接队列（SYN_RCVD 状态）+
     *   已完成连接队列（ESTABLISHED 状态，等待 accept）
     *
     * 调用 listen() 后，套接字开始监听，TCP 状态从 CLOSED 变为 LISTEN
     */
    if (listen(listen_fd, BACKLOG) < 0) {
        log_error("listen() failed");
        perror("listen");
        close(listen_fd);
        return -1;
    }
    snprintf(log_msg, sizeof(log_msg),
             "listen() on %s:%d, waiting for connections...",
             config->host, config->port);
    log_info(log_msg);
    printf("[V0.6] TCP server listening on http://%s:%d\n",
           config->host, config->port);

    /* ===== 步骤 4: accept() 接受一个客户端连接 =====
     *
     * accept(sockfd, cliaddr, addrlen)
     * 从已完成连接队列的队头返回下一个已完成连接。
     * 如果已完成连接队列为空，进程将进入睡眠（阻塞等待）。
     *
     * 返回值：一个全新的套接字描述符（conn_fd），用于与这个客户端的通信。
     * 原来的 listen_fd 继续监听，可以接受新的连接。
     *
     * cliaddr 和 addrlen 用于获取客户端的地址信息（IP + 端口）。
     */
    client_addr_len = sizeof(client_addr);
    conn_fd = accept(listen_fd,
                     (struct sockaddr *)&client_addr,
                     &client_addr_len);
    if (conn_fd < 0) {
        log_error("accept() failed");
        perror("accept");
        close(listen_fd);
        return -1;
    }
    snprintf(log_msg, sizeof(log_msg),
             "accept() connection from %s:%d, conn_fd=%d",
             inet_ntoa(client_addr.sin_addr),
             ntohs(client_addr.sin_port),
             conn_fd);
    log_info(log_msg);
    printf("[V0.6] Accepted connection from %s:%d\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    /* ===== 步骤 5: recv() 接收 HTTP 请求报文 =====
     *
     * recv(sockfd, buf, len, flags)
     * 从 TCP 连接读取数据。如果接收缓冲区中没有数据，进程将阻塞等待。
     *
     * 返回值：
     *   >0  — 实际读取的字节数
     *   =0  — 对方关闭了连接（收到 FIN）
     *   -1  — 出错
     *
     * 客户端（curl/浏览器）发送的典型 HTTP 请求格式：
     *   GET /hello HTTP/1.1\r\n
     *   Host: 127.0.0.1:8080\r\n
     *   User-Agent: curl/8.x\r\n
     *   \r\n
     */
    memset(recv_buf, 0, sizeof(recv_buf));
    n = recv(conn_fd, recv_buf, sizeof(recv_buf) - 1, 0);
    if (n < 0) {
        log_error("recv() failed");
        perror("recv");
        close(conn_fd);
        close(listen_fd);
        return -1;
    }
    if (n == 0) {
        log_info("Client closed connection before sending data");
        close(conn_fd);
        close(listen_fd);
        return 0;
    }
    recv_buf[n] = '\0';

    snprintf(log_msg, sizeof(log_msg),
             "recv() received %zd bytes", n);
    log_info(log_msg);
    printf("[V0.6] Received HTTP request (%zd bytes):\n%s\n", n, recv_buf);


    memset(resp_buf, 0, sizeof(resp_buf));
    handle_request_string(recv_buf, resp_buf, sizeof(resp_buf));

    snprintf(log_msg, sizeof(log_msg),
             "Response generated (%zd bytes)", strlen(resp_buf));
    log_info(log_msg);

    n = send(conn_fd, resp_buf, strlen(resp_buf), 0);
    if (n < 0) {
        log_error("send() failed");
        perror("send");
    } else {
        snprintf(log_msg, sizeof(log_msg),
                 "send() sent %zd bytes", n);
        log_info(log_msg);
        printf("[V0.6] Sent HTTP response (%zd bytes)\n", n);
    }

    /* ===== 步骤 8: close() 关闭套接字，释放资源 =====
     *
     * close(sockfd)
     * 关闭套接字描述符，将套接字标记为 CLOSED 状态。
     * TCP 协议栈会将未发送完的数据继续传输，然后发送 FIN 段关闭连接。
     *
     * 注意：每次 accept 返回新的 conn_fd，因此需要先关闭 conn_fd，
     * 再关闭 listen_fd。（本 V0.6 版本只处理一个连接即退出）
     */
    close(conn_fd);
    close(listen_fd);

    log_info("Connection closed, server exiting normally");
    printf("[V0.6] Server exiting normally after handling one connection\n");

    return 0;
}
