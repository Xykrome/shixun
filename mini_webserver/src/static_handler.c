/*
 * W3D2 static_handler.c — 静态文件处理器实现
 *
 * 功能：
 *   1. URL 路径 → 本地文件的安全映射
 *   2. MIME 类型识别（扩展名 → Content-Type 查表）
 *   3. send_all() 可靠发送（处理 send() 部分发送）
 *   4. serve_static_file() 完整静态文件响应流程
 *
 * 对照 W3D2 知识点：
 *   - 路径规范化：去查询参数、/→/index.html、拒绝 .. 和空字节
 *   - 安全边界：realpath() 验证目标必须在 document root 内
 *   - stat()：st_mode 判断文件类型，st_size 生成 Content-Length
 *   - MIME 映射：文本资源显式 charset=utf-8，二进制资源不加
 *   - 分块发送：8KB 缓冲区 + send_all()，内存与文件大小解耦
 *   - errno 映射：ENOENT→404, EACCES→403, 其他 I/O→500
 */

#include "static_handler.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>

/* ===== MIME 类型映射表 ============================================== */

typedef struct {
    const char *ext;
    const char *mime;
} mime_entry_t;

/*
 * MIME 映射表：
 *   - 文本类型显式添加 charset=utf-8
 *   - 二进制类型不加 charset
 *   - 按扩展名字母序排列，方便二分查找（当前为线性扫描）
 */
static const mime_entry_t MIME_TABLE[] = {
    { "css",   "text/css; charset=utf-8"           },
    { "gif",   "image/gif"                         },
    { "htm",   "text/html; charset=utf-8"           },
    { "html",  "text/html; charset=utf-8"           },
    { "ico",   "image/vnd.microsoft.icon"           },
    { "jpeg",  "image/jpeg"                        },
    { "jpg",   "image/jpeg"                        },
    { "js",    "text/javascript; charset=utf-8"     },
    { "json",  "application/json; charset=utf-8"    },
    { "pdf",   "application/pdf"                   },
    { "png",   "image/png"                         },
    { "svg",   "image/svg+xml"                     },
    { "txt",   "text/plain; charset=utf-8"          },
    { "xml",   "application/xml"                   },
    { "zip",   "application/zip"                   },
};

#define MIME_TABLE_SIZE (sizeof(MIME_TABLE) / sizeof(MIME_TABLE[0]))

/*
 * 从文件路径中提取扩展名并返回 MIME 类型。
 * 查找最后一个 '.' 后的部分，转为小写后查表。
 * 未找到扩展名或未识别类型 → application/octet-stream
 */
const char *get_mime_type(const char *path)
{
    const char *dot;
    char ext[16];
    int i, j;
    size_t table_size = MIME_TABLE_SIZE;

    if (path == NULL) {
        return "application/octet-stream";
    }

    /* 查找最后一个 '.' */
    dot = strrchr(path, '.');
    if (dot == NULL || *(dot + 1) == '\0') {
        return "application/octet-stream";
    }
    dot++;  /* 跳过 '.' */

    /* 复制扩展名并转为小写 */
    for (i = 0; dot[i] != '\0' && i < (int)sizeof(ext) - 1; i++) {
        char c = dot[i];
        /* 遇到 ? 或非扩展名字符时停止 */
        if (c == '?' || c == '/') break;
        ext[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    ext[i] = '\0';

    if (i == 0) {
        return "application/octet-stream";
    }

    /* 查表 */
    for (j = 0; j < (int)table_size; j++) {
        if (strcmp(ext, MIME_TABLE[j].ext) == 0) {
            return MIME_TABLE[j].mime;
        }
    }

    /* 未识别 → 二进制流 */
    return "application/octet-stream";
}

/* ===== send_all() — 可靠发送 ======================================== */

int send_all(int fd, const char *data, size_t len)
{
    size_t sent = 0;
    ssize_t n;

    while (sent < len) {
        n = send(fd, data + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;  /* 被信号中断，重试 */
            }
            /* 其他错误 → 失败 */
            log_warning("send_all() failed");
            return -1;
        }
        if (n == 0) {
            /* 对端关闭 */
            return -1;
        }
        sent += (size_t)n;
    }

    return 0;
}

/* ===== normalize_path() — 路径规范化 ================================ */

int normalize_path(const char *url_path, char *file_path, size_t size)
{
    const char *src;
    char *dst;
    size_t remaining;

    if (url_path == NULL || file_path == NULL || size == 0) {
        return -1;
    }

    /* ---- 1. 复制 WWW_ROOT 到输出缓冲区 ---- */
    {
        size_t root_len = strlen(WWW_ROOT);
        if (root_len + 1 >= size) {
            return -1;
        }
        memcpy(file_path, WWW_ROOT, root_len);
        dst = file_path + root_len;
        remaining = size - root_len;
        *dst = '\0';
    }

    src = url_path;

    /* ---- 2. 安全扫描：拒绝空字节和 .. 路径段 ---- */
    {
        const char *scan = src;
        while (*scan != '\0') {
            if (*scan == '\0') {
                /* 空字节 → 400 */
                return -1;
            }
            /* 检测 /../ 或 /.. 结尾 */
            if (*scan == '/') {
                const char *next = scan + 1;
                if (next[0] == '.' && next[1] == '.' &&
                    (next[2] == '/' || next[2] == '\0' || next[2] == '?')) {
                    return -1;  /* 目录穿越攻击 */
                }
                if (next[0] == '.' && (next[1] == '/' || next[1] == '\0' || next[1] == '?')) {
                    /* /. 路径，跳过但保留（realpath 会处理） */
                }
            }
            scan++;
        }
    }

    /* ---- 3. 处理 / → /index.html ---- */
    if (strcmp(src, "/") == 0) {
        src = "/index.html";
    }

    /* ---- 4. 复制路径，去除查询参数 ---- */
    while (*src != '\0' && *src != '?' && remaining > 1) {
        /* 跳过连续的多个 / */
        if (*src == '/' && *(src + 1) == '/') {
            src++;
            continue;
        }
        *dst++ = *src++;
        remaining--;
    }
    *dst = '\0';

    /* 空路径 → /index.html */
    if (dst == file_path + strlen(WWW_ROOT)) {
        if (remaining > strlen("/index.html")) {
            strcat(file_path, "/index.html");
        } else {
            return -1;
        }
    }

    return 0;
}

/* ===== serve_static_file() — 完整静态文件响应 ====================== */

int serve_static_file(int client_fd, const char *url_path,
                      int *status_code, const char **mime_type,
                      int *body_bytes)
{
    char  raw_path[MAX_PATH_LEN];
    char  safe_path[MAX_PATH_LEN];
    char  www_root_abs[MAX_PATH_LEN];
    struct stat st;
    const char *mime;
    char  header[1024];
    int   file_fd;
    int   total_sent = 0;
    char *resolved;
    int   header_len;

    /* 初始化输出参数 */
    *status_code = 500;
    *mime_type   = "application/octet-stream";
    *body_bytes  = 0;

    /* ---- 1. 路径规范化 ---- */
    if (normalize_path(url_path, raw_path, sizeof(raw_path)) != 0) {
        /*
         * 路径非法分两种情况：
         *   - 包含 .. 的目录穿越攻击 → 403 Forbidden
         *   - 其他格式错误（空字节等） → 400 Bad Request
         */
        if (strstr(url_path, "..") != NULL) {
            *status_code = 403;
        } else {
            *status_code = 400;
        }
        *mime_type   = "text/html; charset=utf-8";

        {
            const char *reason_phrase;
            const char *body;
            if (*status_code == 403) {
                reason_phrase = "403 Forbidden";
                body = "<!DOCTYPE html>\r\n<html>\r\n"
                       "<head><meta charset=\"utf-8\"><title>403</title></head>\r\n"
                       "<body><h1>403 Forbidden</h1></body>\r\n</html>";
            } else {
                reason_phrase = "400 Bad Request";
                body = "<!DOCTYPE html>\r\n<html>\r\n"
                       "<head><meta charset=\"utf-8\"><title>400</title></head>\r\n"
                       "<body><h1>400 Bad Request</h1></body>\r\n</html>";
            }
            *body_bytes = (int)strlen(body);
            header_len = snprintf(header, sizeof(header),
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s", reason_phrase, *body_bytes, body);
            send_all(client_fd, header, (size_t)header_len);
        }
        return header_len;
    }

    /* ---- 2. 获取 document root 的绝对路径 ---- */
    if (realpath(WWW_ROOT, www_root_abs) == NULL) {
        /* document root 本身不存在 */
        *status_code = 500;
        *mime_type   = "text/html; charset=utf-8";
        {
            const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                               "<head><meta charset=\"utf-8\"><title>500</title></head>\r\n"
                               "<body><h1>500 Internal Server Error</h1></body>\r\n</html>";
            *body_bytes = (int)strlen(body);
            header_len = snprintf(header, sizeof(header),
                     "HTTP/1.1 500 Internal Server Error\r\n"
                     "Content-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s", *body_bytes, body);
            send_all(client_fd, header, (size_t)header_len);
            log_error("realpath(WWW_ROOT) failed — document root not found");
        }
        return header_len;
    }

    /* ---- 3. 尝试对完整路径执行 realpath() ---- */
    resolved = realpath(raw_path, safe_path);
    if (resolved == NULL) {
        int e = errno;
        if (e == ENOENT || e == ENOTDIR) {
            /* 文件不存在 → 404 */
            *status_code = 404;
            *mime_type   = "text/html; charset=utf-8";
            {
                const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                                   "<head><meta charset=\"utf-8\"><title>404</title></head>\r\n"
                                   "<body><h1>404 Not Found</h1>"
                                   "<p>The requested URL was not found on this server.</p>"
                                   "</body>\r\n</html>";
                *body_bytes = (int)strlen(body);
                header_len = snprintf(header, sizeof(header),
                         "HTTP/1.1 404 Not Found\r\n"
                         "Content-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: %d\r\n"
                         "Connection: close\r\n"
                         "\r\n"
                         "%s", *body_bytes, body);
                send_all(client_fd, header, (size_t)header_len);
            }
            return header_len;
        } else if (e == EACCES) {
            *status_code = 403;
            *mime_type   = "text/html; charset=utf-8";
            {
                const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                                   "<head><meta charset=\"utf-8\"><title>403</title></head>\r\n"
                                   "<body><h1>403 Forbidden</h1></body>\r\n</html>";
                *body_bytes = (int)strlen(body);
                header_len = snprintf(header, sizeof(header),
                         "HTTP/1.1 403 Forbidden\r\n"
                         "Content-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: %d\r\n"
                         "Connection: close\r\n"
                         "\r\n"
                         "%s", *body_bytes, body);
                send_all(client_fd, header, (size_t)header_len);
            }
            return header_len;
        } else {
            /* 其他错误 → 500 */
            *status_code = 500;
            *mime_type   = "text/html; charset=utf-8";
            {
                const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                                   "<head><meta charset=\"utf-8\"><title>500</title></head>\r\n"
                                   "<body><h1>500 Internal Server Error</h1></body>\r\n</html>";
                *body_bytes = (int)strlen(body);
                header_len = snprintf(header, sizeof(header),
                         "HTTP/1.1 500 Internal Server Error\r\n"
                         "Content-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: %d\r\n"
                         "Connection: close\r\n"
                         "\r\n"
                         "%s", *body_bytes, body);
                send_all(client_fd, header, (size_t)header_len);
                log_error("realpath() failed — internal error");
            }
            return header_len;
        }
    }

    /* ---- 4. 安全检查：验证解析后的路径在 document root 内 ---- */
    {
        size_t root_len = strlen(www_root_abs);
        if (strncmp(safe_path, www_root_abs, root_len) != 0 ||
            (safe_path[root_len] != '/' && safe_path[root_len] != '\0')) {
            /* 路径越界 → 403 */
            *status_code = 403;
            *mime_type   = "text/html; charset=utf-8";
            {
                const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                                   "<head><meta charset=\"utf-8\"><title>403</title></head>\r\n"
                                   "<body><h1>403 Forbidden</h1></body>\r\n</html>";
                *body_bytes = (int)strlen(body);
                header_len = snprintf(header, sizeof(header),
                         "HTTP/1.1 403 Forbidden\r\n"
                         "Content-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: %d\r\n"
                         "Connection: close\r\n"
                         "\r\n"
                         "%s", *body_bytes, body);
                send_all(client_fd, header, (size_t)header_len);
                log_warning("path traversal attempt blocked");
            }
            return header_len;
        }
    }

    /* ---- 5. stat() 获取文件元数据 ---- */
    if (stat(safe_path, &st) != 0) {
        int e = errno;
        if (e == ENOENT || e == ENOTDIR) {
            *status_code = 404;
        } else if (e == EACCES) {
            *status_code = 403;
        } else {
            *status_code = 500;
            log_error("stat() failed — internal error");
        }
        *mime_type = "text/html; charset=utf-8";
        {
            const char *body_fmt;
            const char *reason;
            if (*status_code == 404) {
                reason = "404 Not Found";
                body_fmt = "<!DOCTYPE html>\r\n<html>\r\n"
                           "<head><meta charset=\"utf-8\"><title>404</title></head>\r\n"
                           "<body><h1>404 Not Found</h1></body>\r\n</html>";
            } else if (*status_code == 403) {
                reason = "403 Forbidden";
                body_fmt = "<!DOCTYPE html>\r\n<html>\r\n"
                           "<head><meta charset=\"utf-8\"><title>403</title></head>\r\n"
                           "<body><h1>403 Forbidden</h1></body>\r\n</html>";
            } else {
                reason = "500 Internal Server Error";
                body_fmt = "<!DOCTYPE html>\r\n<html>\r\n"
                           "<head><meta charset=\"utf-8\"><title>500</title></head>\r\n"
                           "<body><h1>500 Internal Server Error</h1></body>\r\n</html>";
            }
            *body_bytes = (int)strlen(body_fmt);
            header_len = snprintf(header, sizeof(header),
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s", reason, *body_bytes, body_fmt);
            send_all(client_fd, header, (size_t)header_len);
        }
        return header_len;
    }

    /* ---- 6. 仅允许发送普通文件 ---- */
    if (!S_ISREG(st.st_mode)) {
        *status_code = 403;
        *mime_type   = "text/html; charset=utf-8";
        {
            const char *body = "<!DOCTYPE html>\r\n<html>\r\n"
                               "<head><meta charset=\"utf-8\"><title>403</title></head>\r\n"
                               "<body><h1>403 Forbidden</h1></body>\r\n</html>";
            *body_bytes = (int)strlen(body);
            header_len = snprintf(header, sizeof(header),
                     "HTTP/1.1 403 Forbidden\r\n"
                     "Content-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s", *body_bytes, body);
            send_all(client_fd, header, (size_t)header_len);
        }
        return header_len;
    }

    /* ---- 7. 确定 MIME 类型 ---- */
    mime = get_mime_type(safe_path);
    *mime_type = mime;

    /* ---- 8. 构造并发送 HTTP 响应头 ---- */
    *status_code = 200;
    *body_bytes  = (int)st.st_size;

    header_len = snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n",
             mime, *body_bytes);

    if (send_all(client_fd, header, (size_t)header_len) != 0) {
        /* 发送响应头失败 */
        log_warning("failed to send response header");
        return header_len;
    }
    total_sent = header_len;

    /* ---- 9. 打开文件 ---- */
    file_fd = open(safe_path, O_RDONLY);
    if (file_fd < 0) {
        /* 文件无法打开 → 500（虽然 stat 成功但 open 失败） */
        *status_code = 500;
        log_error("open() failed — cannot read file after stat succeeded");
        return total_sent;  /* 响应头已发送，无法回退 */
    }

    /* ---- 10. 分块读取 + 可靠发送文件内容 ---- */
    {
        char buf[FILE_BUF_SIZE];
        ssize_t n;

        while ((n = read(file_fd, buf, sizeof(buf))) > 0) {
            if (send_all(client_fd, buf, (size_t)n) != 0) {
                log_warning("send_all() failed while sending file content");
                close(file_fd);
                return total_sent;
            }
            total_sent += (int)n;
        }

        if (n < 0) {
            /* read() 错误 */
            log_error("read() failed while reading file");
        }
    }

    /* ---- 11. 关闭文件 ---- */
    close(file_fd);

    return total_sent;
}
