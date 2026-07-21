/*
 * W3D5 http_server.c — 配置驱动的 HTTP 服务器 V1.5
 *
 * 功能：
 *   epoll 事件循环 + 路由表分发 + 静态文件服务：
 *   1. socket() / bind() / listen() / epoll_create1()  启动监听
 *   2. 用 config 中的 host 和 port 绑定
 *   3. 从 config.routes[] 构建运行时路由表
 *   4. epoll_wait() 等待就绪事件
 *   5. accept() / recv() / 解析 HTTP / 路由分发
 *   6. 路由表匹配 → handler；无匹配 → 静态文件 → 404/405
 *   7. access_log() + log_info() 记录日志
 *   8. epoll_ctl(DEL) + close() 清理连接
 *
 * V1.5 变更：
 *   - host/port/document_root 来自配置文件
 *   - 路由分发由配置驱动，不再硬编码 if/else
 *   - handler 通过注册表查找，未注册名称在启动时拒绝
 *   - 保留 V1.3 全部 HTTP 行为
 */

#include "http_server.h"
#include "http_parser.h"
#include "static_handler.h"
#include "query_handler.h"
#include "auth.h"
#include "session.h"
#include "bearer.h"
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

/* ===== Runtime route entry ============================================= */

typedef struct {
    char    method[MAX_METHOD_LEN];
    char    path[MAX_RPATH_LEN];
    char    auth[MAX_AUTH_LEN];     /* V1.5: "" = public, "basic" = Basic auth */
    Handler fn;
} route_entry_t;

/* ===== Handler wrapper functions (V1.5 unified signature) ============== */

/*
 * GET /search handler.
 * Extracts query string from req->path and delegates to handle_search_request.
 */
static int search_get_handler_impl(int client_fd, const void *req_ptr,
                                    int *status_code, const char **mime_type,
                                    int *body_bytes)
{
    const http_request_t *req = (const http_request_t *)req_ptr;
    const char *query_str = "";
    const char *qmark;

    qmark = strchr(req->path, '?');
    if (qmark) {
        query_str = qmark + 1;
    }

    return handle_search_request(client_fd, "GET", query_str,
                                 status_code, mime_type, body_bytes);
}

/*
 * POST /search handler.
 * Validates Content-Type and Content-Length, then delegates.
 */
static int search_post_handler_impl(int client_fd, const void *req_ptr,
                                     int *status_code, const char **mime_type,
                                     int *body_bytes)
{
    const http_request_t *req = (const http_request_t *)req_ptr;
    const char *content_type = NULL;
    int i;

    /* Look up Content-Type */
    for (i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, "Content-Type") == 0) {
            content_type = req->headers[i].value;
            break;
        }
    }

    /* 415: missing or wrong Content-Type */
    if (content_type == NULL ||
        strstr(content_type, "application/x-www-form-urlencoded") == NULL) {
        const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                           "<head><meta charset=\"utf-8\"><title>415</title></head>\r\n"
                           "<body><h1>415 Unsupported Media Type</h1>"
                           "<p>Content-Type must be application/x-www-form-urlencoded</p>"
                           "</body>\r\n</html>";
        char header[512];
        int hdr_len;
        *status_code = 415;
        *mime_type   = "text/html; charset=utf-8";
        *body_bytes  = (int)strlen(body);
        hdr_len = snprintf(header, sizeof(header),
                   "HTTP/1.1 415 Unsupported Media Type\r\n"
                   "Content-Type: text/html; charset=utf-8\r\n"
                   "Content-Length: %d\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "%s", *body_bytes, body);
        send_all(client_fd, header, (size_t)hdr_len);
        log_warning("/search POST wrong Content-Type, 415 returned");
        return hdr_len;
    }

    /* 413: body too large */
    if (req->content_length > MAX_BODY_SIZE) {
        const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                           "<head><meta charset=\"utf-8\"><title>413</title></head>\r\n"
                           "<body><h1>413 Payload Too Large</h1>"
                           "<p>Request body exceeds 4096 bytes</p>"
                           "</body>\r\n</html>";
        char header[512];
        int hdr_len;
        *status_code = 413;
        *mime_type   = "text/html; charset=utf-8";
        *body_bytes  = (int)strlen(body);
        hdr_len = snprintf(header, sizeof(header),
                   "HTTP/1.1 413 Payload Too Large\r\n"
                   "Content-Type: text/html; charset=utf-8\r\n"
                   "Content-Length: %d\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "%s", *body_bytes, body);
        send_all(client_fd, header, (size_t)hdr_len);
        log_warning("/search POST body too large, 413 returned");
        return hdr_len;
    }

    /* 400: missing Content-Length */
    if (req->content_length <= 0) {
        const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                           "<head><meta charset=\"utf-8\"><title>400</title></head>\r\n"
                           "<body><h1>400 Bad Request</h1>"
                           "<p>POST requires Content-Length</p>"
                           "</body>\r\n</html>";
        char header[512];
        int hdr_len;
        *status_code = 400;
        *mime_type   = "text/html; charset=utf-8";
        *body_bytes  = (int)strlen(body);
        hdr_len = snprintf(header, sizeof(header),
                   "HTTP/1.1 400 Bad Request\r\n"
                   "Content-Type: text/html; charset=utf-8\r\n"
                   "Content-Length: %d\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "%s", *body_bytes, body);
        send_all(client_fd, header, (size_t)hdr_len);
        log_warning("/search POST missing Content-Length, 400 returned");
        return hdr_len;
    }

    /* All checks passed → delegate */
    return handle_search_request(client_fd, "POST", req->body,
                                 status_code, mime_type, body_bytes);
}

/*
 * POST /echo handler (V1.1 legacy).
 */
static int echo_post_handler_impl(int client_fd, const void *req_ptr,
                                   int *status_code, const char **mime_type,
                                   int *body_bytes)
{
    const http_request_t *req = (const http_request_t *)req_ptr;
    char resp_buf[RESP_BUF_SIZE];
    char body_buf[HTTP_BODY_MAX + 256];

    *status_code = 200;
    *mime_type   = "text/plain; charset=utf-8";

    if (req->body_len > 0) {
        snprintf(body_buf, sizeof(body_buf), "Echo: %s", req->body);
    } else {
        snprintf(body_buf, sizeof(body_buf), "Echo: (empty body)");
    }
    *body_bytes = (int)strlen(body_buf);

    snprintf(resp_buf, sizeof(resp_buf),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain; charset=utf-8\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s", *body_bytes, body_buf);

    {
        ssize_t sent = send(client_fd, resp_buf, strlen(resp_buf), 0);
        if (sent < 0) {
            printf("[SERVER] send() failed on fd=%d: %s\n",
                   client_fd, strerror(errno));
        }
        return (int)sent;
    }
}

/*
 * GET /secured handler (V1.5).
 * Serves the protected page.  Auth check is done by middleware before
 * calling this handler, so we just serve the static secured.html file.
 */
static int secured_get_handler_impl(int client_fd, const void *req_ptr,
                                     int *status_code, const char **mime_type,
                                     int *body_bytes)
{
    (void)req_ptr;  /* request already authenticated by middleware */
    return serve_static_file(client_fd, "/secured.html",
                             status_code, mime_type, body_bytes);
}

/* Handler functions with external linkage — registered in config.c */
int search_get_handler(int client_fd, const void *req,
                       int *sc, const char **mt, int *bb)
{ return search_get_handler_impl(client_fd, req, sc, mt, bb); }

int search_post_handler(int client_fd, const void *req,
                        int *sc, const char **mt, int *bb)
{ return search_post_handler_impl(client_fd, req, sc, mt, bb); }

int echo_post_handler(int client_fd, const void *req,
                      int *sc, const char **mt, int *bb)
{ return echo_post_handler_impl(client_fd, req, sc, mt, bb); }

int secured_get_handler(int client_fd, const void *req,
                        int *sc, const char **mt, int *bb)
{ return secured_get_handler_impl(client_fd, req, sc, mt, bb); }

/* ===== Client management =============================================== */

static int add_client(client_info_t *clients, int *client_count,
                      int conn_fd, struct sockaddr_in *client_addr, int epfd)
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

            {
                struct epoll_event ev;
                ev.events = EPOLLIN;
                ev.data.fd = conn_fd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev) < 0) {
                    perror("[SERVER] epoll_ctl(ADD) failed");
                }
            }

            printf("[SERVER] Connection #%d from %s:%d (fd=%d)\n",
                   *client_count, clients[i].ip, clients[i].port, conn_fd);

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

static void remove_client(client_info_t *clients, int i, int *client_count,
                          int epfd, const char *reason)
{
    int fd = clients[i].fd;

    printf("[SERVER] Client %s:%d disconnected (fd=%d) — %s\n",
           clients[i].ip, clients[i].port, fd, reason);

    {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "client disconnected: %s:%d (fd=%d) — %s",
                 clients[i].ip, clients[i].port, fd, reason);
        log_info(msg);
    }

    if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        if (errno != ENOENT) {
            perror("[SERVER] epoll_ctl(DEL) failed");
        }
    }

    close(fd);
    clients[i].fd = -1;
    (*client_count)--;
}

static int find_client_by_fd(client_info_t *clients, int fd)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == fd) return i;
    }
    return -1;
}

/* ===== Session store (V1.5 选做1 — 全局共享) ========================== */

static session_store_t g_session_store;

/* ===== Bearer Token store (V1.5 选做2 — 全局共享) ====================== */

static token_store_t g_token_store;

/* ===== Session authentication middleware ================================= */

/*
 * Validate session for routes with auth="session".
 * Returns 0 on success, -1 on failure (401/403 already sent).
 */
static int validate_session_auth(int client_fd, const http_request_t *req,
                                 int *status_code, const char **mime_type,
                                 int *body_bytes)
{
    char session_id[SESSION_ID_LEN + 1];
    session_t *sess;
    int i;
    const char *cookie_header = NULL;

    /* Find Cookie header */
    for (i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, "Cookie") == 0) {
            cookie_header = req->headers[i].value;
            break;
        }
    }

    if (!cookie_header) {
        /* No cookie — not authenticated */
        log_info("no Cookie header for session-protected route, 401 returned");
        {
            const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                               "<head><meta charset=\"utf-8\"><title>401</title></head>\r\n"
                               "<body><h1>401 Unauthorized</h1>"
                               "<p>Session required. Please <a href=\"/login.html\">login</a>.</p>"
                               "</body>\r\n</html>";
            char resp[1024];
            int len;
            *status_code = 401;
            *mime_type = "text/html; charset=utf-8";
            *body_bytes = (int)strlen(body);
            len = snprintf(resp, sizeof(resp),
                     "HTTP/1.1 401 Unauthorized\r\n"
                     "Content-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n%s", *body_bytes, body);
            send(client_fd, resp, (size_t)len, 0);
        }
        return -1;
    }

    /* Parse session_id from cookie */
    if (parse_cookie(cookie_header, "session_id", session_id,
                     sizeof(session_id)) != 0) {
        log_info("no session_id cookie, 401 returned");
        {
            const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                               "<head><meta charset=\"utf-8\"><title>401</title></head>\r\n"
                               "<body><h1>401 Unauthorized</h1>"
                               "<p>No session found. Please <a href=\"/login.html\">login</a>.</p>"
                               "</body>\r\n</html>";
            char resp[1024];
            int len;
            *status_code = 401;
            *mime_type = "text/html; charset=utf-8";
            *body_bytes = (int)strlen(body);
            len = snprintf(resp, sizeof(resp),
                     "HTTP/1.1 401 Unauthorized\r\n"
                     "Content-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n%s", *body_bytes, body);
            send(client_fd, resp, (size_t)len, 0);
        }
        return -1;
    }

    /* Look up session */
    sess = session_lookup(&g_session_store, session_id);
    if (!sess) {
        log_info("session not found or expired, 401 returned");
        {
            const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                               "<head><meta charset=\"utf-8\"><title>401</title></head>\r\n"
                               "<body><h1>401 Unauthorized</h1>"
                               "<p>Session expired. Please <a href=\"/login.html\">login</a> again.</p>"
                               "</body>\r\n</html>";
            char resp[1024];
            int len;
            *status_code = 401;
            *mime_type = "text/html; charset=utf-8";
            *body_bytes = (int)strlen(body);
            len = snprintf(resp, sizeof(resp),
                     "HTTP/1.1 401 Unauthorized\r\n"
                     "Content-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "Set-Cookie: session_id=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0\r\n"
                     "\r\n%s", *body_bytes, body);
            send(client_fd, resp, (size_t)len, 0);
        }
        return -1;
    }

    /* For state-changing methods, validate CSRF token */
    if (strcmp(req->method, "POST") == 0 ||
        strcmp(req->method, "PUT") == 0 ||
        strcmp(req->method, "DELETE") == 0) {

        char csrf_value[CSRF_TOKEN_LEN + 1];
        int csrf_ok = 0;

        /* Check 1: X-CSRF-Token header (for JS/fetch clients) */
        for (i = 0; i < req->header_count; i++) {
            if (strcasecmp(req->headers[i].key, "X-CSRF-Token") == 0) {
                if (strcmp(req->headers[i].value, sess->csrf_token) == 0) {
                    csrf_ok = 1;
                }
                break;
            }
        }

        /* Check 2: csrf_token in POST body (for HTML form submissions) */
        if (!csrf_ok && req->body[0] != '\0') {
            const char *p = strstr(req->body, "csrf_token=");
            if (p) {
                p += 11; /* strlen("csrf_token=") */
                const char *end = strchr(p, '&');
                int len = end ? (int)(end - p) : (int)strlen(p);
                if (len > 0 && len <= CSRF_TOKEN_LEN) {
                    memcpy(csrf_value, p, (size_t)len);
                    csrf_value[len] = '\0';
                    if (strcmp(csrf_value, sess->csrf_token) == 0) {
                        csrf_ok = 1;
                    }
                }
            }
        }

        if (!csrf_ok) {
            log_warning("CSRF token mismatch, 403 returned");
            {
                const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                                   "<head><meta charset=\"utf-8\"><title>403</title></head>\r\n"
                                   "<body><h1>403 Forbidden</h1>"
                                   "<p>CSRF validation failed.</p>"
                                   "</body>\r\n</html>";
                char resp[1024];
                int len;
                *status_code = 403;
                *mime_type = "text/html; charset=utf-8";
                *body_bytes = (int)strlen(body);
                len = snprintf(resp, sizeof(resp),
                         "HTTP/1.1 403 Forbidden\r\n"
                         "Content-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: %d\r\n"
                         "Connection: close\r\n"
                         "\r\n%s", *body_bytes, body);
                send(client_fd, resp, (size_t)len, 0);
            }
            return -1;
        }
    }

    /* Valid session */
    log_info("session auth succeeded");
    return 0;
}

/* ===== Session handler implementations (V1.5 选做1) ===================== */

/*
 * POST /login handler.
 * Parses username/password from POST body (x-www-form-urlencoded),
 * validates against stored credentials, creates session, returns
 * Set-Cookie + CSRF token.
 */
static int session_login_handler_impl(int client_fd, const void *req_ptr,
                                       int *status_code, const char **mime_type,
                                       int *body_bytes)
{
    const http_request_t *req = (const http_request_t *)req_ptr;
    char username[64]  = {0};
    char password[64]  = {0};
    char cookie_hdr[COOKIE_MAX_LEN];
    char resp_buf[2048];
    char body_buf[1024];
    session_t *sess;
    int resp_len;

    /* Simple form-urlencoded parser: username=...&password=... */
    {
        const char *body = req->body;
        const char *p;

        /* Find username */
        p = strstr(body, "username=");
        if (p) {
            p += 9; /* strlen("username=") */
            const char *amp = strchr(p, '&');
            int len = amp ? (int)(amp - p) : (int)strlen(p);
            if (len >= (int)sizeof(username)) len = (int)sizeof(username) - 1;
            memcpy(username, p, (size_t)len);
            username[len] = '\0';
        }

        /* Find password */
        p = strstr(body, "password=");
        if (p) {
            p += 9; /* strlen("password=") */
            const char *amp = strchr(p, '&');
            int len = amp ? (int)(amp - p) : (int)strlen(p);
            if (len >= (int)sizeof(password)) len = (int)sizeof(password) - 1;
            memcpy(password, p, (size_t)len);
            password[len] = '\0';
        }
    }

    /* Validate credentials — use config from caller context */
    /* (We use hardcoded check against student:lab123 for now,
     *  matching the Basic auth credentials) */
    if (username[0] == '\0' || password[0] == '\0' ||
        strcmp(username, "student") != 0 ||
        strcmp(password, "lab123") != 0) {

        log_info("login failed: invalid credentials");
        *status_code = 401;
        *mime_type = "text/html; charset=utf-8";
        *body_bytes = snprintf(body_buf, sizeof(body_buf),
                     "<!DOCTYPE html>\r\n<html>\r\n"
                     "<head><meta charset=\"utf-8\"><title>Login Failed</title></head>\r\n"
                     "<body><h1>Login Failed</h1>"
                     "<p>Invalid username or password.</p>"
                     "<p><a href=\"/login.html\">Try again</a></p>"
                     "</body>\r\n</html>");

        resp_len = snprintf(resp_buf, sizeof(resp_buf),
                   "HTTP/1.1 401 Unauthorized\r\n"
                   "Content-Type: text/html; charset=utf-8\r\n"
                   "Content-Length: %d\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "%s", *body_bytes, body_buf);
        send(client_fd, resp_buf, (size_t)resp_len, 0);
        return resp_len;
    }

    /* Create session */
    sess = session_create(&g_session_store, username);
    if (!sess) {
        log_error("session_create failed — store full");
        *status_code = 500;
        *mime_type = "text/html; charset=utf-8";
        *body_bytes = snprintf(body_buf, sizeof(body_buf),
                     "<!DOCTYPE html>\r\n<html>\r\n"
                     "<head><meta charset=\"utf-8\"><title>500</title></head>\r\n"
                     "<body><h1>500 Internal Server Error</h1>"
                     "<p>Could not create session.</p>"
                     "</body>\r\n</html>");
        resp_len = snprintf(resp_buf, sizeof(resp_buf),
                   "HTTP/1.1 500 Internal Server Error\r\n"
                   "Content-Type: text/html; charset=utf-8\r\n"
                   "Content-Length: %d\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "%s", *body_bytes, body_buf);
        send(client_fd, resp_buf, (size_t)resp_len, 0);
        return resp_len;
    }

    /* Build success response with Set-Cookie and CSRF token in body */
    build_set_cookie_header(sess->session_id, cookie_hdr, sizeof(cookie_hdr));

    log_info("login succeeded, session created");

    *status_code = 200;
    *mime_type = "text/html; charset=utf-8";
    *body_bytes = snprintf(body_buf, sizeof(body_buf),
                 "<!DOCTYPE html>\r\n<html>\r\n"
                 "<head><meta charset=\"utf-8\"><title>Login Success</title></head>\r\n"
                 "<body><h1>Login Successful</h1>"
                 "<p>Welcome, <strong>%s</strong>!</p>"
                 "<p>Your CSRF Token: <code>%s</code></p>"
                 "<p><a href=\"/dashboard\">Go to Dashboard</a></p>"
                 "<p><a href=\"/\">Home</a></p>"
                 "</body>\r\n</html>",
                 sess->username, sess->csrf_token);

    resp_len = snprintf(resp_buf, sizeof(resp_buf),
               "HTTP/1.1 200 OK\r\n"
               "%s\r\n"
               "Content-Type: text/html; charset=utf-8\r\n"
               "Content-Length: %d\r\n"
               "Connection: close\r\n"
               "Cache-Control: no-store\r\n"
               "\r\n"
               "%s", cookie_hdr, *body_bytes, body_buf);
    send(client_fd, resp_buf, (size_t)resp_len, 0);
    return resp_len;
}

/*
 * POST /logout handler.
 * Destroys the current session and returns a clear-cookie header.
 */
static int session_logout_handler_impl(int client_fd, const void *req_ptr,
                                        int *status_code, const char **mime_type,
                                        int *body_bytes)
{
    const http_request_t *req = (const http_request_t *)req_ptr;
    char cookie_hdr[COOKIE_MAX_LEN];
    char session_id[SESSION_ID_LEN + 1];
    char body_buf[512];
    char resp_buf[1024];
    int resp_len;
    int i;
    const char *cookie_header = NULL;

    /* Find Cookie for cleanup */
    for (i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, "Cookie") == 0) {
            cookie_header = req->headers[i].value;
            break;
        }
    }

    if (cookie_header) {
        parse_cookie(cookie_header, "session_id", session_id,
                     sizeof(session_id));
        session_destroy(&g_session_store, session_id);
        log_info("session destroyed (logout)");
    }

    build_clear_cookie_header(cookie_hdr, sizeof(cookie_hdr));

    *status_code = 200;
    *mime_type = "text/html; charset=utf-8";
    *body_bytes = snprintf(body_buf, sizeof(body_buf),
                 "<!DOCTYPE html>\r\n<html>\r\n"
                 "<head><meta charset=\"utf-8\"><title>Logged Out</title></head>\r\n"
                 "<body><h1>Logged Out</h1>"
                 "<p>You have been logged out successfully.</p>"
                 "<p><a href=\"/login.html\">Login again</a> | <a href=\"/\">Home</a></p>"
                 "</body>\r\n</html>");

    resp_len = snprintf(resp_buf, sizeof(resp_buf),
               "HTTP/1.1 200 OK\r\n"
               "%s\r\n"
               "Content-Type: text/html; charset=utf-8\r\n"
               "Content-Length: %d\r\n"
               "Connection: close\r\n"
               "Cache-Control: no-store\r\n"
               "\r\n"
               "%s", cookie_hdr, *body_bytes, body_buf);
    send(client_fd, resp_buf, (size_t)resp_len, 0);
    return resp_len;
}

/*
 * GET /dashboard handler (session-protected).
 * Displays session info and a logout button with CSRF protection.
 */
static int session_dashboard_handler_impl(int client_fd, const void *req_ptr,
                                           int *status_code,
                                           const char **mime_type,
                                           int *body_bytes)
{
    const http_request_t *req = (const http_request_t *)req_ptr;
    char session_id[SESSION_ID_LEN + 1] = {0};
    char body_buf[1024];
    char resp_buf[1536];
    int resp_len;
    int i;
    const char *cookie_header = NULL;
    session_t *sess = NULL;

    /* Find Cookie header */
    for (i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, "Cookie") == 0) {
            cookie_header = req->headers[i].value;
            break;
        }
    }

    if (cookie_header) {
        parse_cookie(cookie_header, "session_id", session_id,
                     sizeof(session_id));
        sess = session_lookup(&g_session_store, session_id);
    }

    /* Build status-appropriate page */
    if (sess) {
        *status_code = 200;
        *body_bytes = snprintf(body_buf, sizeof(body_buf),
                     "<!DOCTYPE html>\r\n<html>\r\n"
                     "<head><meta charset=\"utf-8\"><title>Dashboard</title></head>\r\n"
                     "<body><h1>&#128274; Dashboard</h1>"
                     "<p>Welcome, <strong>%s</strong>!</p>"
                     "<p>Session: <code>%s...</code></p>"
                     "<p>CSRF Token: <code>%s</code></p>"
                     "<form method=\"POST\" action=\"/logout\">"
                     "<input type=\"hidden\" name=\"csrf_token\" value=\"%s\">"
                     "<button type=\"submit\">Logout</button>"
                     "</form>"
                     "<p><a href=\"/\">Home</a></p>"
                     "</body>\r\n</html>",
                     sess->username, sess->session_id,
                     sess->csrf_token, sess->csrf_token);
    } else {
        *status_code = 401;
        *body_bytes = snprintf(body_buf, sizeof(body_buf),
                     "<!DOCTYPE html>\r\n<html>\r\n"
                     "<head><meta charset=\"utf-8\"><title>401</title></head>\r\n"
                     "<body><h1>401 Unauthorized</h1>"
                     "<p>Session not found. <a href=\"/login.html\">Login</a></p>"
                     "</body>\r\n</html>");
    }

    *mime_type = "text/html; charset=utf-8";

    resp_len = snprintf(resp_buf, sizeof(resp_buf),
               "HTTP/1.1 %d %s\r\n"
               "Content-Type: text/html; charset=utf-8\r\n"
               "Content-Length: %d\r\n"
               "Connection: close\r\n"
               "Cache-Control: no-store\r\n"
               "\r\n"
               "%s",
               *status_code,
               (*status_code == 200) ? "OK" : "Unauthorized",
               *body_bytes, body_buf);
    send(client_fd, resp_buf, (size_t)resp_len, 0);
    return resp_len;
}

/*
 * GET /login handler (V1.5).
 * Simply serves the static login.html page.
 */
static int serve_login_page_impl(int client_fd, const void *req_ptr,
                                  int *status_code, const char **mime_type,
                                  int *body_bytes)
{
    (void)req_ptr;
    return serve_static_file(client_fd, "/login.html",
                             status_code, mime_type, body_bytes);
}

/* External linkage for session handlers */
int serve_login_page(int client_fd, const void *req,
                     int *sc, const char **mt, int *bb)
{ return serve_login_page_impl(client_fd, req, sc, mt, bb); }

int session_login_handler(int client_fd, const void *req,
                          int *sc, const char **mt, int *bb)
{ return session_login_handler_impl(client_fd, req, sc, mt, bb); }

int session_logout_handler(int client_fd, const void *req,
                           int *sc, const char **mt, int *bb)
{ return session_logout_handler_impl(client_fd, req, sc, mt, bb); }

int session_dashboard_handler(int client_fd, const void *req,
                              int *sc, const char **mt, int *bb)
{ return session_dashboard_handler_impl(client_fd, req, sc, mt, bb); }

/* ===== Bearer Token middleware (V1.5 选做2) ============================= */

/*
 * Validate Bearer token for routes with auth="bearer".
 * Returns 0 on success, -1 on failure (401 already sent).
 */
static int validate_bearer_auth(int client_fd, const http_request_t *req,
                                int *status_code, const char **mime_type,
                                int *body_bytes)
{
    char token_str[TOKEN_ID_LEN + 1];
    const char *auth_header = NULL;
    token_entry_t *t;
    int i;

    /* Find Authorization header */
    for (i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, "Authorization") == 0) {
            auth_header = req->headers[i].value;
            break;
        }
    }

    if (!auth_header) {
        log_info("no Authorization header for bearer-protected route, 401");
        {
            const char *body = "{\"error\":\"unauthorized\","
                               "\"error_description\":\"Bearer token required\"}";
            char resp[512];
            int len;
            *status_code = 401;
            *mime_type = "application/json; charset=utf-8";
            *body_bytes = (int)strlen(body);
            len = snprintf(resp, sizeof(resp),
                     "HTTP/1.1 401 Unauthorized\r\n"
                     "Content-Type: application/json; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "WWW-Authenticate: Bearer realm=\"mini_webserver\"\r\n"
                     "\r\n%s", *body_bytes, body);
            send(client_fd, resp, (size_t)len, 0);
        }
        return -1;
    }

    /* Parse Bearer token */
    if (parse_bearer_token(auth_header, token_str, sizeof(token_str)) != 0) {
        log_info("invalid Bearer token format, 401");
        {
            const char *body = "{\"error\":\"invalid_token\","
                               "\"error_description\":\"Malformed Bearer token\"}";
            char resp[512];
            int len;
            *status_code = 401;
            *mime_type = "application/json; charset=utf-8";
            *body_bytes = (int)strlen(body);
            len = snprintf(resp, sizeof(resp),
                     "HTTP/1.1 401 Unauthorized\r\n"
                     "Content-Type: application/json; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n%s", *body_bytes, body);
            send(client_fd, resp, (size_t)len, 0);
        }
        return -1;
    }

    /* Lookup token */
    t = token_lookup(&g_token_store, token_str);
    if (!t) {
        log_info("Bearer token not found or expired, 401");
        {
            const char *body = "{\"error\":\"invalid_token\","
                               "\"error_description\":\"Token not found or expired\"}";
            char resp[512];
            int len;
            *status_code = 401;
            *mime_type = "application/json; charset=utf-8";
            *body_bytes = (int)strlen(body);
            len = snprintf(resp, sizeof(resp),
                     "HTTP/1.1 401 Unauthorized\r\n"
                     "Content-Type: application/json; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n%s", *body_bytes, body);
            send(client_fd, resp, (size_t)len, 0);
        }
        return -1;
    }

    /* Token valid — log without exposing token value */
    log_info("Bearer token auth succeeded");
    return 0;
}

/* ===== Bearer Token handler implementations (V1.5 选做2) ================ */

/*
 * POST /token handler.
 * Validates credentials, creates a Bearer token, returns it as JSON.
 */
static int token_post_handler_impl(int client_fd, const void *req_ptr,
                                    int *status_code, const char **mime_type,
                                    int *body_bytes)
{
    const http_request_t *req = (const http_request_t *)req_ptr;
    char username[64]  = {0};
    char password[64]  = {0};
    token_entry_t *t;
    char body_buf[512];
    char resp_buf[1024];
    int resp_len;

    /* Simple form-urlencoded parser */
    {
        const char *body = req->body;
        const char *p;

        p = strstr(body, "username=");
        if (p) {
            p += 9;
            const char *amp = strchr(p, '&');
            int len = amp ? (int)(amp - p) : (int)strlen(p);
            if (len >= (int)sizeof(username)) len = (int)sizeof(username) - 1;
            memcpy(username, p, (size_t)len);
            username[len] = '\0';
        }

        p = strstr(body, "password=");
        if (p) {
            p += 9;
            const char *amp = strchr(p, '&');
            int len = amp ? (int)(amp - p) : (int)strlen(p);
            if (len >= (int)sizeof(password)) len = (int)sizeof(password) - 1;
            memcpy(password, p, (size_t)len);
            password[len] = '\0';
        }
    }

    /* Validate credentials */
    if (username[0] == '\0' || password[0] == '\0' ||
        strcmp(username, "student") != 0 ||
        strcmp(password, "lab123") != 0) {

        log_info("token request: invalid credentials, 401");
        *status_code = 401;
        *mime_type = "application/json; charset=utf-8";
        *body_bytes = snprintf(body_buf, sizeof(body_buf),
                     "{\"error\":\"invalid_grant\","
                     "\"error_description\":\"Invalid credentials\"}");

        resp_len = snprintf(resp_buf, sizeof(resp_buf),
                   "HTTP/1.1 401 Unauthorized\r\n"
                   "Content-Type: application/json; charset=utf-8\r\n"
                   "Content-Length: %d\r\n"
                   "Connection: close\r\n"
                   "\r\n%s", *body_bytes, body_buf);
        send(client_fd, resp_buf, (size_t)resp_len, 0);
        return resp_len;
    }

    /* Create token */
    t = token_create(&g_token_store, username,
                     "mini_webserver", "api", TOKEN_TIMEOUT_SEC);
    if (!t) {
        log_error("token_create failed — store full");
        *status_code = 500;
        *mime_type = "application/json; charset=utf-8";
        *body_bytes = snprintf(body_buf, sizeof(body_buf),
                     "{\"error\":\"server_error\","
                     "\"error_description\":\"Could not create token\"}");
        resp_len = snprintf(resp_buf, sizeof(resp_buf),
                   "HTTP/1.1 500 Internal Server Error\r\n"
                   "Content-Type: application/json; charset=utf-8\r\n"
                   "Content-Length: %d\r\n"
                   "Connection: close\r\n"
                   "\r\n%s", *body_bytes, body_buf);
        send(client_fd, resp_buf, (size_t)resp_len, 0);
        return resp_len;
    }

    /* Return token as JSON — NOTE: token value NOT logged */
    log_info("Bearer token created successfully");

    *status_code = 200;
    *mime_type = "application/json; charset=utf-8";
    *body_bytes = snprintf(body_buf, sizeof(body_buf),
                 "{\"access_token\":\"%s\","
                 "\"token_type\":\"Bearer\","
                 "\"expires_in\":%d,"
                 "\"issuer\":\"%s\","
                 "\"audience\":\"%s\"}",
                 t->token_id, TOKEN_TIMEOUT_SEC,
                 t->issuer, t->audience);

    resp_len = snprintf(resp_buf, sizeof(resp_buf),
               "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json; charset=utf-8\r\n"
               "Content-Length: %d\r\n"
               "Connection: close\r\n"
               "Cache-Control: no-store\r\n"
               "\r\n%s", *body_bytes, body_buf);
    send(client_fd, resp_buf, (size_t)resp_len, 0);
    return resp_len;
}

/*
 * GET /api/me handler (bearer-protected).
 * Returns user info for the authenticated token holder.
 */
static int api_me_handler_impl(int client_fd, const void *req_ptr,
                                int *status_code, const char **mime_type,
                                int *body_bytes)
{
    const http_request_t *req = (const http_request_t *)req_ptr;
    char token_str[TOKEN_ID_LEN + 1];
    token_entry_t *t;
    const char *auth_header = NULL;
    char body_buf[512];
    char resp_buf[1024];
    int resp_len;
    int i;

    /* Find token (already validated by middleware, but we need it for lookup) */
    for (i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, "Authorization") == 0) {
            auth_header = req->headers[i].value;
            break;
        }
    }

    parse_bearer_token(auth_header, token_str, sizeof(token_str));
    t = token_lookup(&g_token_store, token_str);

    if (!t) {
        *status_code = 401;
        *mime_type = "application/json; charset=utf-8";
        *body_bytes = snprintf(body_buf, sizeof(body_buf),
                     "{\"error\":\"invalid_token\"}");
    } else {
        *status_code = 200;
        *mime_type = "application/json; charset=utf-8";
        *body_bytes = snprintf(body_buf, sizeof(body_buf),
                     "{\"username\":\"%s\","
                     "\"issuer\":\"%s\","
                     "\"audience\":\"%s\","
                     "\"expires_at\":%ld}",
                     t->username, t->issuer, t->audience,
                     (long)t->expires_at);
    }

    resp_len = snprintf(resp_buf, sizeof(resp_buf),
               "HTTP/1.1 %d %s\r\n"
               "Content-Type: application/json; charset=utf-8\r\n"
               "Content-Length: %d\r\n"
               "Connection: close\r\n"
               "Cache-Control: no-store\r\n"
               "\r\n%s",
               *status_code,
               (*status_code == 200) ? "OK" : "Unauthorized",
               *body_bytes, body_buf);
    send(client_fd, resp_buf, (size_t)resp_len, 0);
    return resp_len;
}

/* External linkage for bearer handlers */
int token_post_handler(int client_fd, const void *req,
                        int *sc, const char **mt, int *bb)
{ return token_post_handler_impl(client_fd, req, sc, mt, bb); }

int api_me_handler(int client_fd, const void *req,
                   int *sc, const char **mt, int *bb)
{ return api_me_handler_impl(client_fd, req, sc, mt, bb); }

/* ===== Route table helpers ============================================= */

/*
 * Build a comma-separated list of allowed methods for `target_path`
 * from the route table. Used for the Allow header in 405 responses.
 * Returns the number of allowed methods.
 */
static int build_allow_header(const route_entry_t *routes, int route_count,
                              const char *target_path,
                              char *allow_buf, int allow_size)
{
    int found = 0;
    int pos = 0;
    int i;

    allow_buf[0] = '\0';

    for (i = 0; i < route_count; i++) {
        if (strcmp(routes[i].path, target_path) == 0) {
            if (found > 0 && pos < allow_size - 1) {
                pos += snprintf(allow_buf + pos, (size_t)(allow_size - pos), ", ");
            }
            if (pos < allow_size - 1) {
                pos += snprintf(allow_buf + pos, (size_t)(allow_size - pos),
                                "%s", routes[i].method);
            }
            found++;
        }
    }
    return found;
}

/*
 * Extract the path portion from a URL (strip query string).
 * Writes result into `out` (size `out_size`). Never overflows.
 */
static void extract_path_no_query(const char *url_path, char *out, int out_size)
{
    const char *qmark;
    int len;

    if (!url_path || !out || out_size <= 0) return;
    qmark = strchr(url_path, '?');
    if (qmark) {
        len = (int)(qmark - url_path);
    } else {
        len = (int)strlen(url_path);
    }
    if (len >= out_size) len = out_size - 1;
    memcpy(out, url_path, (size_t)len);
    out[len] = '\0';
}

/*
 * Find a route matching (method, path).
 * Returns index into routes[], or -1 if not found.
 */
static int find_route(const route_entry_t *routes, int route_count,
                      const char *method, const char *path)
{
    int i;
    for (i = 0; i < route_count; i++) {
        if (strcmp(routes[i].method, method) == 0 &&
            strcmp(routes[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * Check if any route matches `path` (regardless of method).
 * Returns 1 if path exists in route table, 0 otherwise.
 */
static int path_in_routes(const route_entry_t *routes, int route_count,
                          const char *path)
{
    int i;
    for (i = 0; i < route_count; i++) {
        if (strcmp(routes[i].path, path) == 0) return 1;
    }
    return 0;
}

/* ===== http_server_run() =============================================== */

int http_server_run(const server_config_t *config, int max_requests)
{
    int                  listen_fd, epfd;
    int                  client_count  = 0;
    int                  request_count = 0;
    struct sockaddr_in   server_addr;
    struct sockaddr_in   client_addr;
    socklen_t            client_addr_len;
    client_info_t        clients[MAX_CLIENTS];
    struct epoll_event   events[MAX_EVENTS];
    route_entry_t        routes[MAX_ROUTES];
    int                  route_count;
    int                  i;

    if (!config) return -1;
    if (max_requests <= 0) max_requests = 2147483647;  /* 0 = unlimited */

    /* Disable stdout buffering */
    setbuf(stdout, NULL);

    /* ---- Initialize document root for static file serving ---- */
    set_document_root(config->document_root);

    /* ---- Initialize session store (V1.5 选做1) ---- */
    session_store_init(&g_session_store);

    /* ---- Initialize token store (V1.5 选做2) ---- */
    token_store_init(&g_token_store);

    /* ---- Build runtime route table from config ---- */
    route_count = 0;
    for (i = 0; i < config->route_count && i < MAX_ROUTES; i++) {
        Handler fn = find_handler_by_name(config->routes[i].handler);
        if (!fn) {
            fprintf(stderr, "[SERVER] FATAL: handler \"%s\" not registered "
                    "(route %d: %s %s)\n",
                    config->routes[i].handler, i,
                    config->routes[i].method, config->routes[i].path);
            return -1;
        }
        strncpy(routes[i].method, config->routes[i].method, MAX_METHOD_LEN - 1);
        routes[i].method[MAX_METHOD_LEN - 1] = '\0';
        strncpy(routes[i].path, config->routes[i].path, MAX_RPATH_LEN - 1);
        routes[i].path[MAX_RPATH_LEN - 1] = '\0';
        strncpy(routes[i].auth, config->routes[i].auth, MAX_AUTH_LEN - 1);
        routes[i].auth[MAX_AUTH_LEN - 1] = '\0';
        routes[i].fn = fn;
        route_count++;
    }

    /* Initialize client slots */
    for (i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

    printf("=== W3D5 HTTP Server V1.5 (config-driven) ===\n");
    printf("[SERVER] Host: %s, Port: %d\n", config->host, config->port);
    printf("[SERVER] Document root: %s\n", config->document_root);
    printf("[SERVER] Log level: %s, Log file: %s\n",
           config->log_level, config->log_file);
    printf("[SERVER] Routes: %d\n", route_count);
    for (i = 0; i < route_count; i++) {
        printf("[SERVER]   %-6s %-20s -> %p\n",
               routes[i].method, routes[i].path, (void *)routes[i].fn);
    }
    printf("[SERVER] Max requests: %d\n", max_requests);

    log_info("HTTP Server V1.5 (config-driven) starting...");

    /* ---- socket() ---- */
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

    /* ---- bind() — use config->host and config->port ---- */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(config->port);

    /* Convert host string to network address */
    if (strcmp(config->host, "0.0.0.0") == 0 || config->host[0] == '\0') {
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (inet_pton(AF_INET, config->host, &server_addr.sin_addr) <= 0) {
            fprintf(stderr, "[SERVER] Invalid host: %s\n", config->host);
            log_error("invalid host address");
            close(listen_fd);
            return -1;
        }
    }

    if (bind(listen_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("[SERVER] bind() failed");
        log_error("bind() failed");
        close(listen_fd);
        return -1;
    }
    printf("[SERVER] bind() to %s:%d\n", config->host, config->port);
    log_info("bind() succeeded");

    /* ---- listen() ---- */
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("[SERVER] listen() failed");
        log_error("listen() failed");
        close(listen_fd);
        return -1;
    }
    printf("[SERVER] listen() on port %d\n", config->port);

    /* ---- epoll_create1() ---- */
    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("[SERVER] epoll_create1() failed");
        log_error("epoll_create1() failed");
        close(listen_fd);
        return -1;
    }
    printf("[SERVER] epoll_create1() succeeded, epfd=%d\n", epfd);

    /* Register listen_fd to epoll */
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

    printf("\n[SERVER] HTTP Server V1.5 is running on http://%s:%d/\n",
           config->host, config->port);
    printf("[SERVER] Process up to %d requests, then exit normally.\n\n",
           max_requests);

    /* ===== Main event loop ===== */
    while (request_count < max_requests) {
        int nfds, j;

        nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("[SERVER] epoll_wait() failed");
            log_error("epoll_wait() failed");
            break;
        }

        for (j = 0; j < nfds; j++) {
            int ready_fd = events[j].data.fd;

            /* ---- New connection ---- */
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
                /* ---- Client data ---- */
                int client_idx = find_client_by_fd(clients, ready_fd);
                if (client_idx == -1) continue;

                {
                    int    client_fd = clients[client_idx].fd;
                    char   temp_buf[8192];
                    ssize_t n;

                    n = recv(client_fd, temp_buf, sizeof(temp_buf) - 1, 0);

                    if (n < 0) {
                        printf("[SERVER] recv() error on fd=%d: %s\n",
                               client_fd, strerror(errno));
                        remove_client(clients, client_idx, &client_count,
                                      epfd, "recv error");
                        continue;
                    }

                    if (n == 0) {
                        printf("[SERVER] recv() returned 0 on fd=%d — client closed\n",
                               client_fd);
                        remove_client(clients, client_idx, &client_count,
                                      epfd, "client closed connection");
                        continue;
                    }

                    /* Append to client buffer */
                    temp_buf[n] = '\0';
                    {
                        int remaining = RECV_BUF_SIZE - clients[client_idx].buf_len - 1;
                        if (n > remaining) n = remaining;
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

                    /* Check if request is complete */
                    if (!is_request_complete(clients[client_idx].recv_buf,
                                             clients[client_idx].buf_len)) {
                        printf("[SERVER] Request incomplete on fd=%d, "
                               "waiting for more data...\n", client_fd);
                        continue;
                    }

                    /* ===== Request complete — parse and dispatch ===== */
                    {
                        http_request_t req;
                        int            status_code  = 500;
                        const char    *mime_type    = "application/octet-stream";
                        int            body_bytes   = 0;
                        struct timespec t_start, t_end;
                        long           elapsed_ms = 0;
                        char           route_path[MAX_RPATH_LEN];
                        int            route_idx;

                        printf("[SERVER] Complete request on fd=%d, "
                               "buffer=%d bytes\n",
                               client_fd, clients[client_idx].buf_len);

                        clock_gettime(CLOCK_MONOTONIC, &t_start);

                        memset(&req, 0, sizeof(req));
                        if (parse_http_request(clients[client_idx].recv_buf,
                                               clients[client_idx].buf_len,
                                               &req) != 0) {
                            /* 400 Bad Request */
                            const char *body = "400 Bad Request\r\n";
                            char resp[256];
                            printf("[SERVER] Failed to parse HTTP request on fd=%d\n",
                                   client_fd);
                            body_bytes = (int)strlen(body) - 2;
                            snprintf(resp, sizeof(resp),
                                     "HTTP/1.1 400 Bad Request\r\n"
                                     "Content-Type: text/plain\r\n"
                                     "Content-Length: %d\r\n"
                                     "Connection: close\r\n"
                                     "\r\n"
                                     "%s", body_bytes, body);
                            send(client_fd, resp, strlen(resp), 0);
                            status_code = 400;
                            mime_type   = "text/plain";
                            access_log(clients[client_idx].ip, "-", "-", "-",
                                       400, mime_type, body_bytes);
                            log_warning("HTTP parse failed, 400 returned");

                        } else {
                            /* ---- V1.5 Route Dispatch ---- */
                            extract_path_no_query(req.path, route_path,
                                                  sizeof(route_path));

                            printf("[SERVER] %s %s (route key: %s %s)\n",
                                   req.method, req.path, req.method, route_path);

                            route_idx = find_route(routes, route_count,
                                                   req.method, route_path);

                            if (route_idx >= 0) {
                                /* V1.5: Check auth before executing handler */
                                if (routes[route_idx].auth[0] != '\0') {
                                    if (strcmp(routes[route_idx].auth, "basic") == 0) {
                                        if (validate_basic_auth(client_fd, &req,
                                                                config,
                                                                &status_code,
                                                                &mime_type,
                                                                &body_bytes) != 0) {
                                            /* Auth failed — 401/400 already sent */
                                            goto request_done;
                                        }
                                    } else if (strcmp(routes[route_idx].auth, "session") == 0) {
                                        if (validate_session_auth(client_fd, &req,
                                                                  &status_code,
                                                                  &mime_type,
                                                                  &body_bytes) != 0) {
                                            /* Auth failed — 401/403 already sent */
                                            goto request_done;
                                        }
                                    } else if (strcmp(routes[route_idx].auth, "bearer") == 0) {
                                        if (validate_bearer_auth(client_fd, &req,
                                                                 &status_code,
                                                                 &mime_type,
                                                                 &body_bytes) != 0) {
                                            /* Auth failed — 401 already sent */
                                            goto request_done;
                                        }
                                    }
                                }

                                /* Route matched — call handler */
                                printf("[SERVER] Route matched: %s %s -> handler\n",
                                       req.method, route_path);
                                routes[route_idx].fn(client_fd, &req,
                                                     &status_code, &mime_type,
                                                     &body_bytes);

                            } else if (path_in_routes(routes, route_count,
                                                      route_path)) {
                                /* Path exists but method not allowed → 405 */
                                char allow_buf[128];
                                const char *body =
                                    "<!DOCTYPE html>\r\n<html>\r\n"
                                    "<head><meta charset=\"utf-8\"><title>405</title></head>\r\n"
                                    "<body><h1>405 Method Not Allowed</h1></body>\r\n</html>";
                                char resp[1024];
                                int hdr_len;

                                build_allow_header(routes, route_count,
                                                   route_path, allow_buf,
                                                   sizeof(allow_buf));

                                status_code = 405;
                                mime_type   = "text/html; charset=utf-8";
                                body_bytes  = (int)strlen(body);

                                hdr_len = snprintf(resp, sizeof(resp),
                                         "HTTP/1.1 405 Method Not Allowed\r\n"
                                         "Content-Type: text/html; charset=utf-8\r\n"
                                         "Content-Length: %d\r\n"
                                         "Connection: close\r\n"
                                         "Allow: %s\r\n"
                                         "\r\n"
                                         "%s", body_bytes, allow_buf, body);
                                send_all(client_fd, resp, (size_t)hdr_len);
                                printf("[SERVER] 405 for %s %s (allowed: %s)\n",
                                       req.method, route_path, allow_buf);

                            } else if (strcmp(req.method, "GET") == 0) {
                                /* No route match, GET → static file */
                                printf("[SERVER] GET %s — routing to static file handler\n",
                                       req.path);
                                serve_static_file(client_fd, req.path,
                                                  &status_code, &mime_type,
                                                  &body_bytes);

                            } else {
                                /* No route match, not GET → 405 */
                                const char *body =
                                    "<!DOCTYPE html>\r\n<html>\r\n"
                                    "<head><meta charset=\"utf-8\"><title>405</title></head>\r\n"
                                    "<body><h1>405 Method Not Allowed</h1></body>\r\n</html>";
                                char resp[1024];
                                int hdr_len;

                                status_code = 405;
                                mime_type   = "text/html; charset=utf-8";
                                body_bytes  = (int)strlen(body);

                                hdr_len = snprintf(resp, sizeof(resp),
                                         "HTTP/1.1 405 Method Not Allowed\r\n"
                                         "Content-Type: text/html; charset=utf-8\r\n"
                                         "Content-Length: %d\r\n"
                                         "Connection: close\r\n"
                                         "Allow: GET\r\n"
                                         "\r\n"
                                         "%s", body_bytes, body);
                                send_all(client_fd, resp, (size_t)hdr_len);
                                printf("[SERVER] 405 for %s %s (no route, not GET)\n",
                                       req.method, route_path);
                            }
                        }

request_done:
                        /* Calculate elapsed time */
                        clock_gettime(CLOCK_MONOTONIC, &t_end);
                        elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                                     (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

                        /* Log */
                        {
                            access_log(clients[client_idx].ip,
                                       req.method, req.path, req.version,
                                       status_code, mime_type, body_bytes);

                            {
                                char log_msg[512];
                                snprintf(log_msg, sizeof(log_msg),
                                         "request handled: %s %s %s -> %d "
                                         "(%s, %d bytes, %ldms)",
                                         req.method, req.path, req.version,
                                         status_code, mime_type, body_bytes,
                                         elapsed_ms);
                                log_info(log_msg);
                            }
                        }

                        request_count++;
                        printf("[SERVER] Request count: %d / %d\n",
                               request_count, max_requests);
                    }

                    /* Clean up connection */
                    remove_client(clients, client_idx, &client_count,
                                  epfd, "request completed");

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
