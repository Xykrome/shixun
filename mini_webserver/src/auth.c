/*
 * W3D5 auth.c — HTTP Basic 认证实现 (V1.5)
 *
 * 功能：
 *   1. Base64 解码（自包含实现）
 *   2. Authorization 头解析与校验
 *   3. 凭据比对
 *   4. 认证失败时发送 401 响应
 *
 * 安全规范（强制）：
 *   - 日志严禁输出 Authorization 头、Base64 编码串、明文密码
 *   - Base64 仅为编码，无加密效果；生产环境必须搭配 HTTPS
 *   - 账号/密码错误统一返回 401（非 403）
 */

#include "auth.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/* ===== Base64 解码 ======================================================= */

/*
 * Base64 字符 → 6-bit 值映射。
 * 非法字符返回 -1。
 */
static int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;  /* 非法字符或 '=' */
}

int base64_decode(const char *input, char *output, int out_size)
{
    int i = 0;
    int out_pos = 0;
    int input_len;

    if (!input || !output || out_size <= 0) return -1;

    input_len = (int)strlen(input);

    while (i < input_len) {
        int vals[4];
        int num_valid = 0;
        int padding = 0;

        /* 收集 4 个有效 Base64 字符 */
        while (num_valid < 4 && i < input_len) {
            char c = input[i++];

            /* 跳过空白字符 */
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                continue;
            }

            /* 遇到 '=' 表示填充开始 */
            if (c == '=') {
                padding++;
                vals[num_valid++] = 0;
                continue;
            }

            {
                int v = base64_value(c);
                if (v < 0) {
                    /* 非法字符 */
                    return -1;
                }
                vals[num_valid++] = v;
            }
        }

        /* 需要至少 2 个字符才能解码 */
        if (num_valid == 0) break;
        if (num_valid == 1) return -1;  /* 不完整的 Base64 组 */

        /* 解码为 3 个字节 */
        if (out_pos < out_size) {
            output[out_pos++] = (char)((vals[0] << 2) | (vals[1] >> 4));
        } else return -1;

        if (num_valid >= 3 && padding < 2) {
            if (out_pos < out_size) {
                output[out_pos++] = (char)(((vals[1] & 0x0f) << 4) | (vals[2] >> 2));
            } else return -1;
        }

        if (num_valid == 4 && padding == 0) {
            if (out_pos < out_size) {
                output[out_pos++] = (char)(((vals[2] & 0x03) << 6) | vals[3]);
            } else return -1;
        }

        /* 填充后停止处理后续字符（= 后不应有有效数据） */
        if (padding > 0) break;
    }

    output[out_pos] = '\0';
    return out_pos;
}

/* ===== Authorization 头查找 ============================================== */

/*
 * 从请求头数组中查找 Authorization 头。
 * 同时检测重复的 Authorization 头（重复 → 返回 -2）。
 *
 * 返回值：
 *   >= 0  - Authorization 头在 headers 中的索引
 *   -1    - 未找到
 *   -2    - 重复（应返回 400）
 */
static int find_authorization_header(const http_request_t *req)
{
    int found_idx = -1;
    int i;

    for (i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, "Authorization") == 0) {
            if (found_idx >= 0) {
                /* 重复的 Authorization 头 → 400 */
                return -2;
            }
            found_idx = i;
        }
    }
    return found_idx;
}

/* ===== 发送 401 响应 ===================================================== */

/*
 * 发送 401 Unauthorized 响应，携带 WWW-Authenticate 头。
 * 注意：此函数不会在日志中输出任何认证相关数据。
 */
static void send_401_response(int client_fd, int *status_code,
                              const char **mime_type, int *body_bytes)
{
    const char *body =
        "<!DOCTYPE html>\r\n<html>\r\n"
        "<head><meta charset=\"utf-8\"><title>401</title></head>\r\n"
        "<body><h1>401 Unauthorized</h1>"
        "<p>Authentication required to access this resource.</p>"
        "</body>\r\n</html>";
    char resp[1024];
    int resp_len;

    *status_code = 401;
    *mime_type   = "text/html; charset=utf-8";
    *body_bytes  = (int)strlen(body);

    resp_len = snprintf(resp, sizeof(resp),
               "HTTP/1.1 401 Unauthorized\r\n"
               "Content-Type: text/html; charset=utf-8\r\n"
               "Content-Length: %d\r\n"
               "Connection: close\r\n"
               "WWW-Authenticate: Basic realm=\"mini_webserver\"\r\n"
               "\r\n"
               "%s", *body_bytes, body);

    {
        ssize_t sent = send(client_fd, resp, (size_t)resp_len, 0);
        if (sent < 0) {
            /* 发送失败，仅记录 fd，不输出敏感的认证数据 */
            char msg[64];
            snprintf(msg, sizeof(msg), "send 401 failed on fd=%d", client_fd);
            log_warning(msg);
        }
    }
}

/*
 * 发送 400 Bad Request 响应（用于重复 Authorization 头等场景）。
 */
static void send_400_response(int client_fd, int *status_code,
                              const char **mime_type, int *body_bytes)
{
    const char *body =
        "<!DOCTYPE html>\r\n<html>\r\n"
        "<head><meta charset=\"utf-8\"><title>400</title></head>\r\n"
        "<body><h1>400 Bad Request</h1></body>\r\n</html>";
    char resp[512];
    int resp_len;

    *status_code = 400;
    *mime_type   = "text/html; charset=utf-8";
    *body_bytes  = (int)strlen(body);

    resp_len = snprintf(resp, sizeof(resp),
               "HTTP/1.1 400 Bad Request\r\n"
               "Content-Type: text/html; charset=utf-8\r\n"
               "Content-Length: %d\r\n"
               "Connection: close\r\n"
               "\r\n"
               "%s", *body_bytes, body);

    send(client_fd, resp, (size_t)resp_len, 0);
}

/* ===== 主认证验证函数 ==================================================== */

int validate_basic_auth(int client_fd, const http_request_t *req,
                        const server_config_t *config,
                        int *status_code, const char **mime_type,
                        int *body_bytes)
{
    int auth_idx;
    const char *auth_value;
    const char *scheme_end;
    char decoded[MAX_CREDENTIAL_LEN];
    int decoded_len;
    char *colon_pos;
    char username[64];
    char password[64];
    int scheme_len;

    /* ---- 1. 查找 Authorization 头 ---- */
    auth_idx = find_authorization_header(req);
    if (auth_idx == -2) {
        /* 重复的 Authorization 头 → 400 */
        log_warning("duplicate Authorization header, 400 returned");
        send_400_response(client_fd, status_code, mime_type, body_bytes);
        return -1;
    }
    if (auth_idx == -1) {
        /* 缺少 Authorization 头 → 401 */
        log_info("missing Authorization header for protected route, 401 returned");
        send_401_response(client_fd, status_code, mime_type, body_bytes);
        return -1;
    }

    auth_value = req->headers[auth_idx].value;

    /* 检查头值长度 */
    if (strlen(auth_value) > MAX_AUTH_HEADER_LEN) {
        log_warning("Authorization header too long, 400 returned");
        send_400_response(client_fd, status_code, mime_type, body_bytes);
        return -1;
    }

    /* ---- 2. 校验认证方案为 "Basic" ---- */
    scheme_end = strchr(auth_value, ' ');
    if (!scheme_end) {
        /* 没有空格分隔 scheme 和 token */
        log_info("invalid Authorization format (no scheme separator), 401 returned");
        send_401_response(client_fd, status_code, mime_type, body_bytes);
        return -1;
    }

    scheme_len = (int)(scheme_end - auth_value);
    if (scheme_len != 5 || strncasecmp(auth_value, "Basic", 5) != 0) {
        /* Scheme 不是 Basic */
        log_info("unsupported auth scheme, 401 returned");
        send_401_response(client_fd, status_code, mime_type, body_bytes);
        return -1;
    }

    /* 跳过空格，获取 Base64 编码串 */
    {
        const char *b64 = scheme_end + 1;
        while (*b64 == ' ' || *b64 == '\t') b64++;

        if (*b64 == '\0') {
            /* Scheme 后无凭据 */
            log_info("empty Basic credentials, 401 returned");
            send_401_response(client_fd, status_code, mime_type, body_bytes);
            return -1;
        }

        /* ---- 3. Base64 解码 ---- */
        decoded_len = base64_decode(b64, decoded, sizeof(decoded) - 1);
        if (decoded_len < 0) {
            /* Base64 格式非法 */
            log_info("invalid Base64 in Authorization header, 401 returned");
            send_401_response(client_fd, status_code, mime_type, body_bytes);
            return -1;
        }
        decoded[decoded_len] = '\0';
    }

    /* ---- 4. 按第一个冒号分割 username:password ---- */
    colon_pos = strchr(decoded, ':');
    if (!colon_pos) {
        /* 无冒号分隔符 */
        log_info("malformed Basic credentials (no colon separator), 401 returned");
        send_401_response(client_fd, status_code, mime_type, body_bytes);
        return -1;
    }

    {
        int user_len = (int)(colon_pos - decoded);
        if (user_len >= (int)sizeof(username)) user_len = (int)sizeof(username) - 1;
        memcpy(username, decoded, (size_t)user_len);
        username[user_len] = '\0';

        strncpy(password, colon_pos + 1, sizeof(password) - 1);
        password[sizeof(password) - 1] = '\0';
    }

    /* ---- 5. 与服务端存储的凭据比对 ---- */
    if (strcmp(username, config->basic_username) != 0 ||
        strcmp(password, config->basic_password) != 0) {
        /* 凭据错误 —— 日志不包含密码！ */
        log_info("invalid credentials for protected route, 401 returned");
        send_401_response(client_fd, status_code, mime_type, body_bytes);
        return -1;
    }

    /* 认证通过 —— 日志不包含凭据详情 */
    log_info("Basic auth succeeded for protected route");
    return 0;
}
