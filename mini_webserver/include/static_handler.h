/*
 * W3D4 static_handler.h — 静态文件处理器头文件 (V1.4)
 *
 * 功能：
 *   1. URL 路径 → 本地文件路径的安全映射（document root 限定）
 *   2. 文件扩展名 → MIME 类型的查表映射
 *   3. send_all() 可靠发送（处理部分发送）
 *   4. serve_static_file() 完整静态文件响应流程
 *
 * V1.4 变更：document_root 可通过 set_document_root() 配置，
 * 不再硬编码为 "www"。默认值为 "www"（保持 V1.2/V1.3 兼容）。
 */

#ifndef STATIC_HANDLER_H
#define STATIC_HANDLER_H

#include <stddef.h>   /* size_t */

#define FILE_BUF_SIZE   8192            /* 文件发送缓冲区大小         */
#define MAX_PATH_LEN    512             /* 文件路径最大长度           */

/*
 * 设置静态资源根目录（替代硬编码的 "www"）。
 * 必须在调用 serve_static_file() 之前设置。
 * 如果未调用，默认使用 "www"。
 */
void set_document_root(const char *root);

/* 返回当前 document_root 字符串 */
const char *get_document_root(void);

/*
 * 根据文件扩展名返回 MIME 类型字符串。
 */
const char *get_mime_type(const char *path);

/*
 * 可靠地发送全部数据（处理部分发送）。
 */
int send_all(int fd, const char *data, size_t len);

/*
 * 安全地规范化请求路径并映射到文件系统路径。
 */
int normalize_path(const char *url_path, char *file_path, size_t size);

/*
 * 处理静态文件请求 —— 完整的静态资源响应流程。
 */
int serve_static_file(int client_fd, const char *url_path,
                      int *status_code, const char **mime_type,
                      int *body_bytes);

#endif /* STATIC_HANDLER_H */
