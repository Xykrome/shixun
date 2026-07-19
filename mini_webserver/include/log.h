/*
 * W3D2 log.h — 增强日志系统头文件
 *
 * 功能：
 *   1. 系统日志：记录服务器运行状态、错误和调试信息
 *      - 支持四级日志：DEBUG / INFO / WARNING / ERROR
 *      - 格式：时间 [LEVEL] 消息
 *   2. 访问日志：记录每个 HTTP 请求的处理结果
 *      - 格式：时间 客户端IP 方法 URL HTTP版本 状态码 MIME 响应字节数
 *
 * 对照 W3D2 知识点：
 *   - 日志分级：DEBUG < INFO < WARNING < ERROR
 *   - 系统日志关注服务器自身事件
 *   - 访问日志关注客户端请求的处理结果（含 MIME 类型）
 */

#ifndef LOG_H
#define LOG_H

#include <stddef.h>   /* size_t */

/*
 * 初始化日志系统。
 *
 * 参数：
 *   sys_log_path    - 系统日志文件路径（如 "logs/system.log"）
 *   access_log_path - 访问日志文件路径（如 "logs/access.log"）
 *
 * 返回值：
 *    0  - 成功
 *   -1  - 至少一个文件打开失败
 */
int log_init(const char *sys_log_path, const char *access_log_path);

/* 系统日志：DEBUG 级别 —— 开发调试用 */
void log_debug(const char *message);

/* 系统日志：INFO 级别 —— 正常运行事件 */
void log_info(const char *message);

/* 系统日志：WARNING 级别 —— 需要关注但不影响运行 */
void log_warning(const char *message);

/* 系统日志：ERROR 级别 —— 错误事件，影响当前请求 */
void log_error(const char *message);

/*
 * 访问日志：记录一次 HTTP 请求的处理结果。
 *
 * 参数：
 *   client_ip     - 客户端 IP 地址
 *   method        - HTTP 方法（GET / POST）
 *   path          - 请求路径
 *   http_version  - HTTP 版本（如 "HTTP/1.1"）
 *   status_code   - HTTP 状态码（如 200）
 *   mime_type     - 响应的 MIME 类型（如 "text/html; charset=utf-8"）
 *   response_size - 响应体字节数
 */
void access_log(const char *client_ip, const char *method,
                const char *path, const char *http_version,
                int status_code, const char *mime_type, int response_size);

/* 关闭日志系统（关闭并刷新两个日志文件） */
void log_close(void);

#endif /* LOG_H */
