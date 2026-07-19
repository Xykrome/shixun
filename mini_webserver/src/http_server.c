/*
 * W3D3 http_server.c — 基于 epoll 的 HTTP 服务器 V1.3
 *
 * 功能：
 *   结合 epoll 事件循环 + 静态文件服务 + 动态查询处理：
 *   1. socket() / bind() / listen() / epoll_create1()  启动监听
 *   2. epoll_ctl(ADD listen_fd)                         注册监听套接字
 *   3. epoll_wait()                                     等待就绪事件
 *   4. accept() / epoll_ctl(ADD client_fd)              接受新连接
 *   5. recv() → 追加到客户端接收缓冲区                    读取请求
 *   6. find_header_end() → 判断请求头是否完整             协议解析
 *   7. parse_http_request() → 提取 method/path/headers/body
 *   8. 路由分发（V1.3）：
 *        GET /search       → 搜索表单 或 查询（含查询参数）
 *        POST /search       → 解析表单请求体并查询
 *        GET *              → serve_static_file() 静态文件服务
 *        POST /echo         → 200 OK + 回显请求体（V1.1 兼容）
 *        其他方法            → 405 Method Not Allowed
 *   9. 动态查询：URL 解码 → 参数校验 → 数据文件查询 → HTML 生成
 *  10. access_log() + log_info()                         记录日志
 *  11. epoll_ctl(DEL) + close()                          清理连接
 *
 * 技术限制（验收标准）：
 *   - 纯 epoll + 单线程事件循环
 *   - 未使用 select、多线程、多进程或线程池
 *   - 默认 LT 模式 + EPOLLIN
 */

#include "http_server.h"
#include "http_parser.h"
#include "static_handler.h"
#include "query_handler.h"
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
#include <time.h>

/*
 * 添加新客户端到客户端列表并注册到 epoll
 */
static int add_client(client_info_t *clients, int *client_count,
                      int conn_fd, struct sockaddr_in *client_addr,
                      int epfd)
{
    int i;

    if (*client_count >= MAX_CLIENTS) {
        fprintf(stderr, "[SERVER] Max clients (%d) reached, rejecting\n", MAX_CLIENTS);
        return -1;
    }

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

            /* ===== 验收标准：epoll_ctl(EPOLL_CTL_ADD) ===== */
            {
                struct epoll_event ev;
                ev.events = EPOLLIN;          /* LT 模式 + EPOLLIN */
                ev.data.fd = conn_fd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev) < 0) {
                    perror("[SERVER] epoll_ctl(ADD) failed");
                }
            }

            printf("[SERVER] Connection #%d from %s:%d (fd=%d)\n",
                   *client_count, clients[i].ip, clients[i].port, conn_fd);

            /* 系统日志：新客户端连接 */
            {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "new client connected: %s:%d (fd=%d)",
                         clients[i].ip, clients[i].port, conn_fd);
                log_info(msg);
            }
            return 0;
        }
    }

    return -1;
}

/*
 * 移除客户端（清理连接）
 *
 * ===== 验收标准：epoll_ctl(DEL) + close =====
 */
static void remove_client(client_info_t *clients, int i, int *client_count,
                          int epfd, const char *reason)
{
    int fd = clients[i].fd;

    printf("[SERVER] Client %s:%d disconnected (fd=%d) — %s\n",
           clients[i].ip, clients[i].port, fd, reason);

    /* 系统日志：客户端断开 */
    {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "client disconnected: %s:%d (fd=%d) — %s",
                 clients[i].ip, clients[i].port, fd, reason);
        log_info(msg);
    }

    /* epoll_ctl DEL */
    if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        if (errno != ENOENT) {
            perror("[SERVER] epoll_ctl(DEL) failed");
        }
    }

    close(fd);
    clients[i].fd = -1;
    (*client_count)--;
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

/* ===== http_server_run() ==============================================
 *
 * 主函数：启动基于 epoll 的 HTTP 服务器 (W3D3 V1.3)
 *
 * 整体流程：
 *   socket → bind → listen → epoll_create1 →
 *   epoll_ctl(ADD listen_fd) →
 *   while (request_count < max_requests) {
 *       epoll_wait() →
 *       如果 listen_fd 就绪 → accept → add_client → epoll_ctl(ADD)
 *       如果 client_fd 就绪 → recv → 追加缓冲区 →
 *           判断请求完整性 → 解析 HTTP 请求 →
 *           路由生成响应 → send → 记录日志 →
 *           epoll_ctl(DEL) + close → request_count++
 *   }
 *   清理 → 正常退出
 * ==================================================================== */
int http_server_run(int port, int max_requests)
{
    int                  listen_fd;
    int                  epfd;
    int                  client_count  = 0;
    int                  request_count = 0;
    struct sockaddr_in   server_addr;
    struct sockaddr_in   client_addr;
    socklen_t            client_addr_len;
    client_info_t        clients[MAX_CLIENTS];
    struct epoll_event   events[MAX_EVENTS];
    int                  i;

    if (port <= 0) port = DEFAULT_PORT;
    if (max_requests <= 0) max_requests = 10;

    /* 禁用 stdout 缓冲 */
    setbuf(stdout, NULL);

    /* 初始化客户端槽位 */
    for (i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

    printf("=== W3D3 HTTP Server V1.3 (epoll + search) ===\n");
    printf("[SERVER] Max requests: %d\n", max_requests);

    /* 系统日志 */
    log_info("HTTP Server V1.3 starting...");

    /* ===== socket() ===== */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("[SERVER] socket() failed");
        log_error("socket() failed");
        return -1;
    }
    printf("[SERVER] socket() created, fd=%d\n", listen_fd);

    /* SO_REUSEADDR */
    {
        int optval = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                       &optval, sizeof(optval)) < 0) {
            perror("[SERVER] setsockopt(SO_REUSEADDR) failed");
            log_warning("setsockopt(SO_REUSEADDR) failed");
            close(listen_fd);
            return -1;
        }
    }

    /* ===== bind() ===== */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("[SERVER] bind() failed");
        log_error("bind() failed");
        close(listen_fd);
        return -1;
    }
    printf("[SERVER] bind() to 0.0.0.0:%d\n", port);
    log_info("bind() succeeded");

    /* ===== listen() ===== */
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("[SERVER] listen() failed");
        log_error("listen() failed");
        close(listen_fd);
        return -1;
    }
    printf("[SERVER] listen() on port %d\n", port);

    /* ===== epoll_create1() ===== */
    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("[SERVER] epoll_create1() failed");
        log_error("epoll_create1() failed");
        close(listen_fd);
        return -1;
    }
    printf("[SERVER] epoll_create1() succeeded, epfd=%d\n", epfd);

    /* 注册 listen_fd 到 epoll */
    {
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = listen_fd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
            perror("[SERVER] epoll_ctl(ADD listen_fd) failed");
            log_error("epoll_ctl(ADD listen_fd) failed");
            close(epfd);
            close(listen_fd);
            return -1;
        }
    }
    printf("[SERVER] listen_fd (fd=%d) registered to epoll (epfd=%d)\n",
           listen_fd, epfd);

    printf("\n[SERVER] HTTP Server V1.3 is running on http://127.0.0.1:%d/\n", port);
    printf("[SERVER] Process up to %d requests, then exit normally.\n\n", max_requests);

    /* ===== 主事件循环 ===== */
    while (request_count < max_requests) {
        int nfds;
        int j;

        nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("[SERVER] epoll_wait() failed");
            log_error("epoll_wait() failed");
            break;
        }

        for (j = 0; j < nfds; j++) {
            int ready_fd = events[j].data.fd;

            /* ===== listen_fd 可读 → 新连接 ===== */
            if (ready_fd == listen_fd) {
                client_addr_len = sizeof(client_addr);
                int conn_fd = accept(listen_fd,
                                     (struct sockaddr *)&client_addr,
                                     &client_addr_len);
                if (conn_fd < 0) {
                    if (errno != EINTR) {
                        perror("[SERVER] accept() failed");
                        log_warning("accept() failed");
                    }
                } else {
                    if (add_client(clients, &client_count,
                                   conn_fd, &client_addr, epfd) != 0) {
                        close(conn_fd);
                    }
                }

            } else {
                /* ===== client_fd 可读 → HTTP 数据 ===== */
                int client_idx = find_client_by_fd(clients, ready_fd);
                if (client_idx == -1) {
                    continue;
                }

                {
                    int    client_fd = clients[client_idx].fd;
                    char   temp_buf[8192];
                    ssize_t n;

                    /* ===== recv() 读取数据 ===== */
                    n = recv(client_fd, temp_buf, sizeof(temp_buf) - 1, 0);

                    if (n < 0) {
                        printf("[SERVER] recv() error on fd=%d: %s\n",
                               client_fd, strerror(errno));
                        remove_client(clients, client_idx, &client_count,
                                      epfd, "recv error");
                        continue;
                    }

                    if (n == 0) {
                        /*
                         * recv() == 0 → 客户端关闭连接
                         */
                        printf("[SERVER] recv() returned 0 on fd=%d — client closed\n",
                               client_fd);
                        remove_client(clients, client_idx, &client_count,
                                      epfd, "client closed connection");
                        continue;
                    }

                    /* ===== 追加数据到客户端接收缓冲区 ===== */
                    temp_buf[n] = '\0';
                    {
                        int remaining = RECV_BUF_SIZE - clients[client_idx].buf_len - 1;
                        if (n > remaining) {
                            n = remaining;
                        }
                        memcpy(clients[client_idx].recv_buf + clients[client_idx].buf_len,
                               temp_buf, n);
                        clients[client_idx].buf_len += n;
                        clients[client_idx].recv_buf[clients[client_idx].buf_len] = '\0';
                    }

                    printf("[SERVER] recv() %zd bytes from %s:%d (fd=%d), "
                           "buffer=%d bytes\n",
                           n, clients[client_idx].ip,
                           clients[client_idx].port, client_fd,
                           clients[client_idx].buf_len);

                    /* ===== 判断 HTTP 请求是否完整 ===== */
                    if (!is_request_complete(clients[client_idx].recv_buf,
                                             clients[client_idx].buf_len)) {
                        /*
                         * 请求不完整，继续等待更多数据。
                         * W3D1 核心知识点：一次 recv() ≠ 一个完整请求
                         */
                        printf("[SERVER] Request incomplete on fd=%d, "
                               "waiting for more data...\n", client_fd);
                        continue;
                    }

                    /* ===== 请求完整，开始解析 ===== */
                    {
                        http_request_t req;
                        int            status_code  = 500;
                        const char    *mime_type    = "application/octet-stream";
                        int            body_bytes   = 0;
                        struct timespec t_start, t_end;
                        long           elapsed_ms = 0;

                        printf("[SERVER] Complete request on fd=%d, "
                               "buffer=%d bytes\n",
                               client_fd, clients[client_idx].buf_len);

                        /* 记录请求处理开始时间 */
                        clock_gettime(CLOCK_MONOTONIC, &t_start);

                        /*
                         * ===== 解析 HTTP 请求 =====
                         * 提取 method, path, version, headers, body
                         */
                        memset(&req, 0, sizeof(req));
                        if (parse_http_request(clients[client_idx].recv_buf,
                                               clients[client_idx].buf_len,
                                               &req) != 0) {
                            /* 解析失败 → 400 Bad Request */
                            const char *body = "400 Bad Request\r\n";
                            char resp[256];

                            printf("[SERVER] Failed to parse HTTP request on fd=%d\n",
                                   client_fd);
                            body_bytes = (int)strlen(body) - 2; /* 不计算 \r\n */
                            snprintf(resp, sizeof(resp),
                                     "HTTP/1.1 400 Bad Request\r\n"
                                     "Content-Type: text/plain\r\n"
                                     "Content-Length: %d\r\n"
                                     "Connection: close\r\n"
                                     "\r\n"
                                     "%s",
                                     body_bytes, body);
                            send(client_fd, resp, strlen(resp), 0);

                            status_code = 400;
                            mime_type   = "text/plain";

                            access_log(clients[client_idx].ip, "-", "-", "-",
                                       400, mime_type, body_bytes);
                            log_warning("HTTP parse failed, 400 returned");

                        } else if (strcmp(req.method, "GET") == 0) {
                            /*
                             * ===== GET 请求 → 路由分发 (V1.3) =====
                             * 检查 /search 路由（动态查询），
                             * 其余路径交由 serve_static_file() 静态文件服务处理。
                             */

                            /* 检查是否为 /search 路径（含可选查询参数） */
                            if (strncmp(req.path, "/search", 7) == 0 &&
                                (req.path[7] == '\0' || req.path[7] == '?')) {
                                /*
                                 * ===== GET /search → 动态查询 (V1.3) =====
                                 * - 无参数：返回搜索表单
                                 * - 有参数：解析查询字符串并查询
                                 */
                                const char *query_str = "";
                                if (req.path[7] == '?') {
                                    query_str = req.path + 8;  /* 跳过 "/search?" */
                                }

                                printf("[SERVER] GET /search (query=%s) — routing to query handler\n",
                                       query_str);
                                handle_search_request(client_fd, "GET", query_str,
                                                      &status_code, &mime_type, &body_bytes);

                            } else {
                                /*
                                 * ===== 其他 GET → 静态文件服务 (V1.2) =====
                                 */
                                printf("[SERVER] GET %s — routing to static file handler\n",
                                       req.path);
                                serve_static_file(client_fd, req.path,
                                                 &status_code, &mime_type, &body_bytes);
                            }

                        } else if (strcmp(req.method, "POST") == 0 &&
                                   strncmp(req.path, "/search", 7) == 0 &&
                                   (req.path[7] == '\0' || req.path[7] == '?')) {
                            /*
                             * ===== POST /search → 动态查询 (V1.3) =====
                             * 检查 Content-Type 和 Content-Length，
                             * 解析请求体中的表单数据并查询。
                             */
                            const char *content_type = NULL;
                            int i;

                            /* 查找 Content-Type 请求头 */
                            for (i = 0; i < req.header_count; i++) {
                                if (strcasecmp(req.headers[i].key, "Content-Type") == 0) {
                                    content_type = req.headers[i].value;
                                    break;
                                }
                            }

                            /* 检查 Content-Type */
                            if (content_type == NULL ||
                                strstr(content_type, "application/x-www-form-urlencoded") == NULL) {
                                /* 不支持的 Content-Type → 415 */
                                const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                                                   "<head><meta charset=\"utf-8\"><title>415</title></head>\r\n"
                                                   "<body><h1>415 Unsupported Media Type</h1>"
                                                   "<p>仅支持 application/x-www-form-urlencoded</p>"
                                                   "</body>\r\n</html>";
                                char resp[1024];
                                status_code = 415;
                                mime_type   = "text/html; charset=utf-8";
                                body_bytes  = (int)strlen(body);
                                snprintf(resp, sizeof(resp),
                                         "HTTP/1.1 415 Unsupported Media Type\r\n"
                                         "Content-Type: text/html; charset=utf-8\r\n"
                                         "Content-Length: %d\r\n"
                                         "Connection: close\r\n"
                                         "\r\n"
                                         "%s", body_bytes, body);
                                send(client_fd, resp, strlen(resp), 0);
                                log_warning("/search POST missing Content-Type, 415 returned");

                            } else if (req.content_length > MAX_BODY_SIZE) {
                                /* 请求体过大 → 413 */
                                const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                                                   "<head><meta charset=\"utf-8\"><title>413</title></head>\r\n"
                                                   "<body><h1>413 Payload Too Large</h1>"
                                                   "<p>请求体不能超过 4096 字节</p>"
                                                   "</body>\r\n</html>";
                                char resp[1024];
                                status_code = 413;
                                mime_type   = "text/html; charset=utf-8";
                                body_bytes  = (int)strlen(body);
                                snprintf(resp, sizeof(resp),
                                         "HTTP/1.1 413 Payload Too Large\r\n"
                                         "Content-Type: text/html; charset=utf-8\r\n"
                                         "Content-Length: %d\r\n"
                                         "Connection: close\r\n"
                                         "\r\n"
                                         "%s", body_bytes, body);
                                send(client_fd, resp, strlen(resp), 0);
                                log_warning("/search POST body too large, 413 returned");

                            } else if (req.content_length <= 0) {
                                /* 缺少 Content-Length → 400 */
                                const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                                                   "<head><meta charset=\"utf-8\"><title>400</title></head>\r\n"
                                                   "<body><h1>400 Bad Request</h1>"
                                                   "<p>POST 请求缺少 Content-Length</p>"
                                                   "</body>\r\n</html>";
                                char resp[1024];
                                status_code = 400;
                                mime_type   = "text/html; charset=utf-8";
                                body_bytes  = (int)strlen(body);
                                snprintf(resp, sizeof(resp),
                                         "HTTP/1.1 400 Bad Request\r\n"
                                         "Content-Type: text/html; charset=utf-8\r\n"
                                         "Content-Length: %d\r\n"
                                         "Connection: close\r\n"
                                         "\r\n"
                                         "%s", body_bytes, body);
                                send(client_fd, resp, strlen(resp), 0);
                                log_warning("/search POST missing Content-Length, 400 returned");

                            } else {
                                /* 请求体完整 → 处理查询 */
                                printf("[SERVER] POST /search (body=%s) — routing to query handler\n",
                                       req.body);
                                handle_search_request(client_fd, "POST", req.body,
                                                      &status_code, &mime_type, &body_bytes);
                            }

                        } else if (strcmp(req.method, "POST") == 0 &&
                                   strcmp(req.path, "/echo") == 0) {
                            /*
                             * ===== POST /echo → 动态路由 (V1.1 兼容) =====
                             */
                            char resp_buf[RESP_BUF_SIZE];
                            char body_buf[HTTP_BODY_MAX + 256];

                            status_code = 200;
                            mime_type   = "text/plain; charset=utf-8";

                            if (req.body_len > 0) {
                                snprintf(body_buf, sizeof(body_buf),
                                         "Echo: %s", req.body);
                            } else {
                                snprintf(body_buf, sizeof(body_buf),
                                         "Echo: (empty body)");
                            }
                            body_bytes = (int)strlen(body_buf);
                            snprintf(resp_buf, sizeof(resp_buf),
                                     "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: text/plain; charset=utf-8\r\n"
                                     "Content-Length: %d\r\n"
                                     "Connection: close\r\n"
                                     "\r\n"
                                     "%s",
                                     body_bytes, body_buf);

                            {
                                ssize_t sent = send(client_fd, resp_buf,
                                                    strlen(resp_buf), 0);
                                if (sent < 0) {
                                    printf("[SERVER] send() failed on fd=%d: %s\n",
                                           client_fd, strerror(errno));
                                } else {
                                    printf("[SERVER] send() %zd bytes to %s:%d (fd=%d)\n",
                                           sent, clients[client_idx].ip,
                                           clients[client_idx].port, client_fd);
                                }
                            }

                        } else {
                            /*
                             * ===== 其他方法/POST 非 /echo 路径 → 405 Method Not Allowed =====
                             * 指导书要求：GET 进入静态资源处理，其他方法返回 405。
                             * /search 路径允许 GET 和 POST。
                             */
                            const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                                               "<head><meta charset=\"utf-8\"><title>405</title></head>\r\n"
                                               "<body><h1>405 Method Not Allowed</h1></body>\r\n</html>";
                            char resp[1024];
                            const char *allow_methods;

                            status_code = 405;
                            mime_type   = "text/html; charset=utf-8";
                            body_bytes  = (int)strlen(body);

                            /* /search 路径允许 GET 和 POST，其他路径仅 GET */
                            if (strncmp(req.path, "/search", 7) == 0 &&
                                (req.path[7] == '\0' || req.path[7] == '?')) {
                                allow_methods = "GET, POST";
                            } else {
                                allow_methods = "GET";
                            }

                            snprintf(resp, sizeof(resp),
                                     "HTTP/1.1 405 Method Not Allowed\r\n"
                                     "Content-Type: text/html; charset=utf-8\r\n"
                                     "Content-Length: %d\r\n"
                                     "Connection: close\r\n"
                                     "Allow: %s\r\n"
                                     "\r\n"
                                     "%s",
                                     body_bytes, allow_methods, body);
                            send(client_fd, resp, strlen(resp), 0);
                        }

                        /* 计算处理耗时 */
                        clock_gettime(CLOCK_MONOTONIC, &t_end);
                        elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                                     (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

                        /*
                         * ===== 记录日志 =====
                         */
                        {
                            /* 访问日志（V1.2 包含 MIME 类型） */
                            access_log(clients[client_idx].ip,
                                       req.method, req.path,
                                       req.version,
                                       status_code, mime_type, body_bytes);

                            /* 系统日志 */
                            {
                                char log_msg[512];
                                snprintf(log_msg, sizeof(log_msg),
                                         "request handled: %s %s %s -> %d (%s, %d bytes, %ldms)",
                                         req.method, req.path, req.version,
                                         status_code, mime_type, body_bytes, elapsed_ms);
                                log_info(log_msg);
                            }
                        }

                        request_count++;
                        printf("[SERVER] Request count: %d / %d\n",
                               request_count, max_requests);
                    }

                    /*
                     * ===== 清理客户端连接 =====
                     * 每个 HTTP 请求处理完毕后关闭连接
                     * epoll_ctl(DEL) + close
                     */
                    remove_client(clients, client_idx, &client_count,
                                  epfd, "request completed");

                    /* 达到最大请求数 → 退出 */
                    if (request_count >= max_requests) {
                        printf("[SERVER] Max requests (%d) reached, shutting down...\n",
                               max_requests);
                        log_info("max_requests reached, shutting down");
                        goto shutdown;
                    }
                }
            }
        }
    }

shutdown:
    /* ===== 清理所有连接 ===== */
    printf("\n[SERVER] Shutting down...\n");
    printf("[SERVER] Total requests processed: %d\n", request_count);
    log_info("server shutting down, cleaning up connections");

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
    log_info("server stopped normally");

    return 0;
}
