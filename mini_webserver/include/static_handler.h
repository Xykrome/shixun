/*
 * W3D2 static_handler.h — 静态文件处理器头文件
 *
 * 功能：
 *   1. URL 路径 → 本地文件路径的安全映射（document root 限定）
 *   2. 文件扩展名 → MIME 类型的查表映射
 *   3. send_all() 可靠发送（处理部分发送）
 *   4. serve_static_file() 完整静态文件响应流程
 *
 * 对照 W3D2 知识点：
 *   - 路径规范化：去除查询参数、映射 / 到 /index.html、拒绝 .. 和空字节
 *   - 安全边界：realpath() 校验结果必须在 document root 内
 *   - stat() 获取文件元数据：类型（S_ISREG）、大小（st_size）
 *   - MIME 映射：文本资源显式 charset=utf-8，二进制资源不加 charset
 *   - 分块发送：8KB 固定缓冲区 + send_all() 处理部分发送
 *   - 错误映射：ENOENT/ENOTDIR→404, EACCES→403, 其他→500
 */

#ifndef STATIC_HANDLER_H
#define STATIC_HANDLER_H

#include <stddef.h>   /* size_t */

#define WWW_ROOT        "www"           /* document root 目录        */
#define FILE_BUF_SIZE   8192            /* 文件发送缓冲区大小         */
#define MAX_PATH_LEN    512             /* 文件路径最大长度           */

/*
 * 根据文件扩展名返回 MIME 类型字符串。
 *
 * 文本资源带 charset=utf-8，二进制资源不带。
 * 未知扩展名返回 "application/octet-stream"。
 *
 * 参数：
 *   path - 文件路径（会提取扩展名并转为小写）
 *
 * 返回值：MIME 类型字符串（静态内存，不需要释放）
 */
const char *get_mime_type(const char *path);

/*
 * 可靠地发送全部数据（处理部分发送）。
 *
 * 参数：
 *   fd   - 目标 socket fd
 *   data - 待发送数据
 *   len  - 数据长度
 *
 * 返回值：
 *    0  - 成功发送全部数据
 *   -1  - 发送失败
 */
int send_all(int fd, const char *data, size_t len);

/*
 * 安全地规范化请求路径并映射到文件系统路径。
 *
 * 处理步骤：
 *   1. 去除 ? 后的查询参数
 *   2. 检查空字节和 ..
 *   3. 映射 / 到 /index.html
 *   4. 拼接 WWW_ROOT + 规范化路径
 *
 * 参数：
 *   url_path  - HTTP 请求中的原始路径
 *   file_path - 输出的完整文件系统路径
 *   size      - file_path 缓冲区大小
 *
 * 返回值：
 *    0  - 成功
 *   -1  - 路径非法（400 Bad Request）
 */
int normalize_path(const char *url_path, char *file_path, size_t size);

/*
 * 处理静态文件请求 —— 完整的静态资源响应流程。
 *
 * 流程：
 *   1. normalize_path()          — 路径规范化
 *   2. realpath()                — 安全校验（拒绝目录穿越）
 *   3. stat()                    — 获取文件类型和大小
 *   4. get_mime_type()           — 确定 Content-Type
 *   5. 构造并发送 HTTP 响应头
 *   6. open() + 分块 read() + send_all() — 发送文件内容
 *   7. close(file_fd)            — 关闭文件
 *
 * 参数：
 *   client_fd   - 客户端 socket fd
 *   url_path    - HTTP 请求中的原始路径
 *   status_code - 输出：HTTP 状态码（用于日志）
 *   mime_type   - 输出：MIME 类型（用于日志）
 *   body_bytes  - 输出：响应体字节数（用于日志）
 *
 * 返回值：
 *   >= 0  - 发送的总字节数（含响应头+响应体）
 *   -1    - 内部 I/O 错误
 */
int serve_static_file(int client_fd, const char *url_path,
                      int *status_code, const char **mime_type,
                      int *body_bytes);

#endif /* STATIC_HANDLER_H */
