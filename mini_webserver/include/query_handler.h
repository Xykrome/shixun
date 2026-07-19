/*
 * W3D3 query_handler.h — 动态查询处理器头文件
 *
 * 功能：
 *   1. URL 解码（%XX 和 + → 空格）
 *   2. 查询字符串解析（key=value&key=value）
 *   3. HTML 特殊字符转义（& < > " '）
 *   4. 参数校验（class: 4位数字, keyword: 1-64字节）
 *   5. 数据文件查询（data/<class>.txt 中匹配 keyword）
 *   6. 动态 HTML 页面生成（搜索表单 / 查询结果 / 错误页）
 *
 * 对照 W3D3 知识点：
 *   - GET 参数在 URL 查询字符串，POST 参数在请求体
 *   - application/x-www-form-urlencoded 格式
 *   - URL 解码顺序：原始参数 → URL解码 → UTF-8校验 → 长度/格式校验 → 业务使用
 *   - class 只允许 4 位数字，映射到 data/<class>.txt
 *   - keyword 1~64 字节，拒绝控制字符和路径穿越
 *   - HTML 输出前转义，防止注入
 */

#ifndef QUERY_HANDLER_H
#define QUERY_HANDLER_H

#define DATA_DIR         "data"          /* 数据文件目录               */
#define MAX_BODY_SIZE    4096            /* POST 请求体上限            */
#define MAX_KEYWORD_LEN  64              /* keyword 最大字节数         */
#define CLASS_DIGITS     4               /* class 固定位数             */
#define MAX_RESULT_HTML  65536           /* 结果 HTML 最大字节数       */

/*
 * URL 解码：将 %XX 转换为对应字节，+ 转换为空格。
 * 返回值：0 成功，-1 格式错误（非法 % 序列）。
 */
int url_decode(const char *src, char *dst, int dst_size);

/*
 * 解析查询字符串 "class=2011&keyword=%E7%94%B7"。
 * 提取 class 和 keyword 参数值（已 URL 解码）。
 * 返回值：0 成功，-1 缺少必要参数或格式错误。
 */
int parse_query_string(const char *query,
                       char *class_buf, int class_size,
                       char *keyword_buf, int keyword_size);

/*
 * HTML 实体转义：将 & < > " ' 替换为对应的 HTML 实体。
 */
void html_escape(const char *src, char *dst, int dst_size);

/*
 * 校验 class 参数：必须恰好 4 位数字。
 * 返回值：1 有效，0 无效。
 */
int validate_class(const char *class_str);

/*
 * 校验 keyword 参数：1~64 字节，拒绝控制字符、..、/、\、空字节。
 * 返回值：1 有效，0 无效。
 */
int validate_keyword(const char *keyword);

/*
 * 查询数据文件 data/<class>.txt，匹配包含 keyword 的行。
 * 结果以 HTML 表格行（<tr><td>...</td></tr>）格式写入 results_buf。
 * 参数：
 *   class_str   - 班级编号（已校验的 4 位数字）
 *   keyword     - 查询关键词（已验证）
 *   results_buf - 输出：HTML 表格行
 *   buf_size    - 输出缓冲区大小
 *   match_count - 输出：匹配的记录数
 * 返回值：0 成功，-1 数据文件不存在或读取失败。
 */
int query_records(const char *class_str, const char *keyword,
                  char *results_buf, int buf_size, int *match_count);

/*
 * 生成搜索表单 HTML（GET /search 无参数时返回）。
 */
void generate_search_form_html(char *html, int html_size);

/*
 * 生成查询结果 HTML 页面。
 * class_str 和 keyword 应已经过验证和转义。
 */
void generate_result_page_html(const char *class_str, const char *keyword,
                               const char *table_rows, int match_count,
                               char *html, int html_size);

/*
 * 生成错误页面 HTML。
 * title 和 message 应为纯文本（内部会做 HTML 转义）。
 */
void generate_error_page_html(int status_code, const char *title,
                              const char *message,
                              char *html, int html_size);

/*
 * 处理 /search 请求的主入口。
 *
 * 参数：
 *   client_fd  - 客户端 socket
 *   method     - "GET" 或 "POST"
 *   query_str  - GET: 查询字符串（? 之后的部分，可为 NULL 表示无参数）
 *                POST: 请求体内容（已 URL 编码的表单数据）
 *   status_code - 输出：HTTP 状态码
 *   mime_type   - 输出：MIME 类型（用于日志）
 *   body_bytes  - 输出：响应体字节数
 *
 * 返回值：发送的总字节数（含响应头+响应体）。
 */
int handle_search_request(int client_fd, const char *method,
                          const char *query_str,
                          int *status_code, const char **mime_type,
                          int *body_bytes);

#endif /* QUERY_HANDLER_H */
