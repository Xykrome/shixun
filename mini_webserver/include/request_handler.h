#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include <stddef.h>   /* size_t */

/*
 * V0.1-V0.5: 处理单个 HTTP 请求文件，将响应写入输出文件。
 *
 * 请求文件格式：
 *   <METHOD> <path>
 *   例如: GET /hello
 *
 * 路由规则：
 *   /hello          → HTTP 200 + "Hello, Web!"
 *   /users/<name>   → 查找用户，输出 FOUND <username> <password> <phone>
 *                      或 NOT_FOUND
 *   其他            → HTTP 404 Not Found
 *
 * 参数：
 *   req_path  - 请求文件路径（如 "request/hello.req"）
 *   out_path  - 输出文件路径（如 "outputs/hello.out"）
 */
void handle_request(const char *req_path, const char *out_path);

/*
 * V0.6: 处理 HTTP 请求字符串，生成 HTTP 响应字符串。
 *
 * 用于真正的 TCP 网络服务器（socket/recv → 解析 → 路由 → send）。
 *
 * 输入 request 是完整的 HTTP 请求报文，如：
 *   GET /hello HTTP/1.1\r\n
 *   Host: 127.0.0.1:8080\r\n
 *   \r\n
 *
 * 函数解析请求行（METHOD PATH PROTOCOL），根据 path 路由：
 *   /hello          → HTTP/1.1 200 OK + 响应体 "Hello, Web!"
 *   /users/<name>   → HTTP/1.1 200 OK + 响应体 "FOUND ..." 或 "NOT_FOUND"
 *   其他            → HTTP/1.1 404 Not Found
 *
 * 参数：
 *   request       - 输入的 HTTP 请求报文（以 '\0' 结尾的字符串）
 *   response      - 输出的 HTTP 响应报文缓冲区
 *   response_size - 输出缓冲区大小
 */
void handle_request_string(const char *request, char *response, size_t response_size);

#endif