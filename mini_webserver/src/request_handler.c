#include "request_handler.h"
#include "http_response.h"
#include "user_store.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== V0.1-V0.5: 文件模式请求处理 ===== */
void handle_request(const char *req_path, const char *out_path)
{
    FILE *req_file;
    FILE *out;
    char method[16];
    char path[256];
    char log_msg[512];

    req_file = fopen(req_path, "r");
    if (req_file == NULL) {
        snprintf(log_msg, sizeof(log_msg),
                 "Failed to open request file: %s", req_path);
        log_error(log_msg);
        return;
    }

    /* 解析请求行: METHOD PATH */
    if (fscanf(req_file, "%15s %255s", method, path) != 2) {
        snprintf(log_msg, sizeof(log_msg),
                 "Failed to parse request in: %s", req_path);
        log_error(log_msg);
        fclose(req_file);
        return;
    }
    fclose(req_file);

    /* 确保输出目录存在（由 .gitkeep 保证，这里只打开文件） */
    out = fopen(out_path, "w");
    if (out == NULL) {
        snprintf(log_msg, sizeof(log_msg),
                 "Failed to open output file: %s", out_path);
        log_error(log_msg);
        return;
    }

    snprintf(log_msg, sizeof(log_msg),
             "Handling %s %s → %s", method, path, out_path);
    log_info(log_msg);

    /*
     * 路由分发：
     *   /hello          → 返回 HTTP 200 及 "Hello, Web!" 响应体
     *   /users/<name>   → 在 CSV 中查找用户，返回 FOUND 或 NOT_FOUND
     *   其他路径         → 返回 HTTP 404
     */
    if (strcmp(path, "/hello") == 0) {
        /* ---- hello 路由 ---- */
        const char *body = "Hello, Web!";
        fprintf(out,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 12\r\n"
            "\r\n"
            "%s",
            body);
        log_info("Route: /hello → 200 OK");

    } else if (strncmp(path, "/users/", 7) == 0 && strlen(path) > 7) {
        /* ---- 用户查找路由 ---- */
        const char *username = path + 7;
        user_node_t *users = NULL;
        user_node_t *cur;
        int found = 0;

        if (load_users("data/users.csv", &users) != 0) {
            fprintf(out, "ERROR: Failed to load user data");
            log_error("Failed to load user data for /users/ request");
            fclose(out);
            return;
        }

        cur = users;
        while (cur != NULL) {
            if (strcmp(cur->username, username) == 0) {
                fprintf(out, "FOUND %s %s %s",
                        cur->username, cur->password, cur->phone);
                found = 1;
                snprintf(log_msg, sizeof(log_msg),
                         "Route: /users/%s → FOUND", username);
                log_info(log_msg);
                break;
            }
            cur = cur->next;
        }

        if (!found) {
            fprintf(out, "NOT_FOUND");
            snprintf(log_msg, sizeof(log_msg),
                     "Route: /users/%s → NOT_FOUND", username);
            log_info(log_msg);
        }

        free_users(users);

    } else {
        /* ---- 404 路由 ---- */
        fprintf(out, "HTTP/1.1 404 Not Found\r\n\r\n");
        snprintf(log_msg, sizeof(log_msg),
                 "Route: %s → 404 Not Found", path);
        log_info(log_msg);
    }

    fclose(out);
}

/* ===== V0.6: TCP 字符串模式请求处理 =====
 *
 * 与 handle_request() 核心路由逻辑相同，但：
 *   输入从"请求文件路径"变为"HTTP 请求报文字符串"
 *   输出从"输出文件"变为"HTTP 响应报文字符串（通过参数返回）"
 *
 * 解析字段：
 *   请求行:  METHOD SP PATH SP PROTOCOL CRLF
 *   例如:    GET /hello HTTP/1.1\r\n
 *
 * 只处理请求行中的路径部分进行路由分发，
 * 后续版本将扩展为更完整的 HTTP/1.1 解析。
 */
void handle_request_string(const char *request, char *response, size_t response_size)
{
    char method[16];
    char path[256];
    char protocol[32];
    char log_msg[512];
    const char *body;
    int body_len;
    int written;

    if (request == NULL || response == NULL || response_size == 0) {
        return;
    }

    /* 初始化为空字符串 */
    response[0] = '\0';

    if (sscanf(request, "%15s %255s %31s", method, path, protocol) < 3) {
        /* 解析失败，返回 400 Bad Request */
        log_error("handle_request_string: failed to parse HTTP request line");
        snprintf(response, response_size,
                 "HTTP/1.1 400 Bad Request\r\n"
                 "Content-Type: text/plain\r\n"
                 "\r\n"
                 "Failed to parse request line");
        return;
    }

    snprintf(log_msg, sizeof(log_msg),
             "V0.6 handling: %s %s %s", method, path, protocol);
    log_info(log_msg);

    if (strcmp(method, "GET") != 0) {
        body = "Method Not Allowed";
        body_len = (int)strlen(body);
        snprintf(response, response_size,
                 "HTTP/1.1 405 Method Not Allowed\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: %d\r\n"
                 "\r\n"
                 "%s",
                 body_len, body);
        snprintf(log_msg, sizeof(log_msg),
                 "Route: %s %s → 405 Method Not Allowed", method, path);
        log_info(log_msg);
        return;
    }

    if (strcmp(path, "/hello") == 0) {
        /* ===== /hello 路由 → HTTP 200 OK ===== */
        body = "Hello, Web!";
        body_len = (int)strlen(body);
        written = snprintf(response, response_size,
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: %d\r\n"
                 "\r\n"
                 "%s",
                 body_len, body);
        if (written < 0 || (size_t)written >= response_size) {
            log_error("handle_request_string: response truncated for /hello");
        }
        log_info("Route: /hello → 200 OK");

    } else if (strncmp(path, "/users/", 7) == 0 && strlen(path) > 7) {
        /* ===== /users/<name> 路由 → 用户查找 ===== */
        const char *username = path + 7;
        user_node_t *users = NULL;
        user_node_t *cur;
        int found = 0;

        /* 加载用户数据 */
        if (load_users("data/users.csv", &users) != 0) {
            body = "Internal Server Error: Failed to load user data";
            body_len = (int)strlen(body);
            snprintf(response, response_size,
                     "HTTP/1.1 500 Internal Server Error\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %d\r\n"
                     "\r\n"
                     "%s",
                     body_len, body);
            log_error("Failed to load user data for /users/ request");
            return;
        }

        /* 遍历链表查找用户 */
        cur = users;
        while (cur != NULL) {
            if (strcmp(cur->username, username) == 0) {
                found = 1;
                break;
            }
            cur = cur->next;
        }

        if (found) {
            /* 返回用户信息 */
            char user_info[256];
            snprintf(user_info, sizeof(user_info),
                     "FOUND %s %s %s",
                     cur->username, cur->password, cur->phone);
            body = user_info;
            body_len = (int)strlen(body);
            snprintf(response, response_size,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %d\r\n"
                     "\r\n"
                     "%s",
                     body_len, body);
            snprintf(log_msg, sizeof(log_msg),
                     "Route: /users/%s → FOUND", username);
        } else {
            body = "NOT_FOUND";
            body_len = (int)strlen(body);
            snprintf(response, response_size,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %d\r\n"
                     "\r\n"
                     "%s",
                     body_len, body);
            snprintf(log_msg, sizeof(log_msg),
                     "Route: /users/%s → NOT_FOUND", username);
        }
        log_info(log_msg);

        free_users(users);

    } else {
        /* ===== 其他路径 → HTTP 404 Not Found ===== */
        body = "404 Not Found";
        body_len = (int)strlen(body);
        snprintf(response, response_size,
                 "HTTP/1.1 404 Not Found\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: %d\r\n"
                 "\r\n"
                 "%s",
                 body_len, body);
        snprintf(log_msg, sizeof(log_msg),
                 "Route: %s → 404 Not Found", path);
        log_info(log_msg);
    }
}