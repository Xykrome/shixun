/*
 * W3D5 auth.h — HTTP Basic 认证头文件 (V1.5)
 *
 * 功能：
 *   1. 从 HTTP 请求中提取并解析 Authorization 头
 *   2. Base64 解码
 *   3. 按首个冒号分割 username:password
 *   4. 与服务器存储的凭据比对
 *   5. 校验失败时直接发送 401 响应
 *
 * V1.5 新增：为受保护路由提供认证中间件。
 */

#ifndef AUTH_H
#define AUTH_H

#include "http_parser.h"  /* http_request_t, http_header_t */
#include "config.h"       /* server_config_t               */

#define MAX_AUTH_HEADER_LEN  512   /* Authorization 头值最大长度  */
#define MAX_CREDENTIAL_LEN   128   /* 解码后凭据最大长度          */

/*
 * 验证 HTTP Basic 认证凭据。
 *
 * 流程：
 *   1. 从 req->headers 中查找唯一的 Authorization 头
 *   2. 校验 Scheme 为 "Basic"
 *   3. Base64 解码
 *   4. 按第一个 ':' 分割为 username 和 password
 *   5. 与 config 中存储的凭据比对
 *
 * 参数：
 *   client_fd   - 客户端套接字（用于发送 401 响应）
 *   req         - 解析后的 HTTP 请求
 *   config      - 服务器配置（含 basic_username / basic_password）
 *   status_code - 输出：HTTP 状态码（401 时设置）
 *   mime_type   - 输出：MIME 类型
 *   body_bytes  - 输出：响应体长度
 *
 * 返回值：
 *    0  - 认证通过
 *   -1  - 认证失败（已发送 401/400 响应）
 */
int validate_basic_auth(int client_fd, const http_request_t *req,
                        const server_config_t *config,
                        int *status_code, const char **mime_type,
                        int *body_bytes);

/*
 * Base64 解码（自包含实现，无外部依赖）。
 *
 * 将 Base64 编码的字符串解码为原始字节。
 * 自动跳过空白字符，正确处理填充字符 '='。
 * Base64 是编码而非加密，本函数不涉及密码学操作。
 *
 * 参数：
 *   input     - Base64 编码的输入字符串
 *   output    - 解码输出缓冲区
 *   out_size  - 输出缓冲区最大长度
 *
 * 返回值：
 *   >= 0  - 解码后的字节数
 *   -1    - 解码失败（非法字符或格式错误）
 */
int base64_decode(const char *input, char *output, int out_size);

#endif /* AUTH_H */
