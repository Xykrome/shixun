/*
 * W3D1 http_parser.c — HTTP 请求解析器实现
 *
 * 功能：
 *   1. find_header_end()      — 查找 \r\n\r\n 分隔符
 *   2. is_request_complete()  — 判断请求是否完整
 *   3. parse_http_request()   — 解析完整的 HTTP 请求
 *
 * 对照 W3D1 知识点：
 *   - TCP 只提供字节流，请求可能分多次到达
 *   - 查找 \r\n\r\n 判断请求头完整性
 *   - 解析 Content-Length 判断请求体长度
 *   - 支持 GET（无请求体）和 POST（有请求体）
 */

#include "http_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int find_header_end(const char *buf, int buf_len)
{
    int i;

    if (buf == NULL || buf_len < 4) {
        return -1;
    }

    /* 查找 \r\n\r\n */
    for (i = 0; i <= buf_len - 4; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            return i + 4;  /* 返回请求体开始位置 */
        }
    }

    return -1;  /* 未找到，请求头不完整 */
}

/*
 * 从请求头部分解析 Content-Length 的值
 */
static int get_content_length(const char *header_part, int header_len)
{
    const char *p = header_part;
    const char *end = header_part + header_len;

    while (p < end) {
        /* 跳过空行 */
        while (p < end && (*p == '\r' || *p == '\n')) p++;
        if (p >= end) break;

        /* 找到行尾 */
        const char *line_end = p;
        while (line_end < end && !(*line_end == '\r' && line_end + 1 < end && *(line_end + 1) == '\n')) {
            line_end++;
        }

        /* 检查是否是 Content-Length 头 */
        if (strncasecmp(p, "Content-Length:", 15) == 0) {
            const char *val = p + 15;
            /* 跳过空格 */
            while (val < line_end && *val == ' ') val++;
            /* 解析数字 */
            int cl = 0;
            while (val < line_end && isdigit((unsigned char)*val)) {
                cl = cl * 10 + (*val - '0');
                val++;
            }
            return cl;
        }

        p = line_end;
        /* 跳过 \r\n */
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }

    return 0;  /* 无 Content-Length 头（如 GET 请求） */
}

int is_request_complete(const char *buf, int buf_len)
{
    int header_end;
    int content_length;

    if (buf == NULL || buf_len <= 0) {
        return 0;
    }

    /* 先查找请求头结束标记 */
    header_end = find_header_end(buf, buf_len);
    if (header_end < 0) {
        return 0;  /* 请求头还不完整 */
    }

    /* 读取 Content-Length */
    content_length = get_content_length(buf, header_end);

    if (content_length == 0) {
        /* 没有 Content-Length，视为请求完整（GET 请求通常无请求体） */
        return 1;
    }

    /* 检查请求体是否接收完整 */
    if (buf_len - header_end >= content_length) {
        return 1;
    }

    return 0;  /* 请求体不完整 */
}

/*
 * 解析一行请求头 "Key: Value\r\n"
 */
static int parse_header_line(const char *line, int line_len, http_header_t *header)
{
    const char *colon;
    int key_len;

    /* 查找冒号 */
    colon = memchr(line, ':', line_len);
    if (colon == NULL) {
        return -1;  /* 不合法的头部行 */
    }

    key_len = (int)(colon - line);
    if (key_len <= 0 || key_len >= HTTP_HEADER_KEY_LEN) {
        return -1;
    }

    /* 复制 key */
    memcpy(header->key, line, key_len);
    header->key[key_len] = '\0';

    /* 跳过冒号和空格，复制 value */
    colon++;  /* 跳过冒号 */
    while (colon < line + line_len && *colon == ' ') colon++;

    int val_len = (int)(line + line_len - colon);
    if (val_len <= 0 || val_len >= HTTP_HEADER_VAL_LEN) {
        /* 允许空值 */
        header->value[0] = '\0';
        return 0;
    }

    memcpy(header->value, colon, val_len);
    header->value[val_len] = '\0';

    return 0;
}

int parse_http_request(const char *buf, int buf_len, http_request_t *req)
{
    const char *p;
    const char *header_end;
    int header_len;
    int line_len;
    const char *line_start;
    const char *body_start;
    int i;

    if (buf == NULL || req == NULL || buf_len <= 0) {
        return -1;
    }

    /* 初始化输出结构 */
    memset(req, 0, sizeof(*req));
    req->content_length = 0;

    /* 查找请求头结束位置 */
    i = find_header_end(buf, buf_len);
    if (i < 0) {
        return -1;  /* 请求头不完整 */
    }
    header_end = buf + i;
    header_len = i;

    /* ===== 解析请求行：METHOD SP PATH SP VERSION CRLF ===== */
    p = buf;

    /* 查找第一个空格之前的内容 → METHOD */
    {
        const char *sp1 = memchr(p, ' ', header_end - p);
        if (sp1 == NULL) {
            return -1;  /* 不合法的请求行 */
        }

        line_len = (int)(sp1 - p);
        if (line_len <= 0 || line_len >= HTTP_METHOD_LEN) {
            return -1;
        }
        memcpy(req->method, p, line_len);
        req->method[line_len] = '\0';
        p = sp1 + 1;  /* 跳过空格 */
    }

    /* 查找第二个空格之前的内容 → PATH */
    {
        const char *sp2 = memchr(p, ' ', header_end - p);
        if (sp2 == NULL) {
            return -1;
        }

        line_len = (int)(sp2 - p);
        if (line_len <= 0 || line_len >= HTTP_PATH_LEN) {
            return -1;
        }
        memcpy(req->path, p, line_len);
        req->path[line_len] = '\0';
        p = sp2 + 1;
    }

    /* 查找 CRLF 之前的内容 → VERSION */
    {
        const char *crlf = p;
        while (crlf < header_end) {
            if (*crlf == '\r' && crlf + 1 < header_end && *(crlf + 1) == '\n') {
                break;
            }
            crlf++;
        }
        if (crlf >= header_end) {
            return -1;
        }

        line_len = (int)(crlf - p);
        if (line_len <= 0 || line_len >= HTTP_VERSION_LEN) {
            return -1;
        }
        memcpy(req->version, p, line_len);
        req->version[line_len] = '\0';
        p = crlf + 2;  /* 跳过 \r\n */
    }

    /* ===== 解析请求头 ===== */
    req->header_count = 0;
    while (p < header_end && req->header_count < HTTP_HEADER_COUNT) {
        /* 跳过空行（\r\n — 请求头结束标志） */
        if (*p == '\r' && p + 1 < header_end && *(p + 1) == '\n') {
            break;
        }

        /* 查找行尾 \r\n */
        line_start = p;
        while (p < header_end) {
            if (*p == '\r' && p + 1 < header_end && *(p + 1) == '\n') {
                break;
            }
            p++;
        }

        if (p >= header_end) break;

        line_len = (int)(p - line_start);
        if (line_len > 0 && line_len < 512) {
            parse_header_line(line_start, line_len,
                              &req->headers[req->header_count]);
            req->header_count++;
        }

        p += 2;  /* 跳过 \r\n */
    }

    /* ===== 解析请求体（如果有 Content-Length）===== */
    req->content_length = get_content_length(buf, header_len);
    body_start = buf + header_len;

    if (req->content_length > 0) {
        int body_available = buf_len - header_len;
        int copy_len = req->content_length;
        if (copy_len > body_available) {
            copy_len = body_available;
        }
        if (copy_len > HTTP_BODY_MAX - 1) {
            copy_len = HTTP_BODY_MAX - 1;
        }
        if (copy_len > 0) {
            memcpy(req->body, body_start, copy_len);
            req->body[copy_len] = '\0';
            req->body_len = copy_len;
        }
    }

    return 0;
}
