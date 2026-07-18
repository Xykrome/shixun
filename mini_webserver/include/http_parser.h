/*
 * W3D1 http_parser.h — HTTP 请求解析器头文件
 *
 * 功能：
 *   1. 从字节流中查找 \r\n\r\n 判断请求头是否完整
 *   2. 解析请求行：METHOD PATH HTTP/VERSION
 *   3. 解析 Content-Length 头
 *   4. 判断请求体是否接收完整（POST 场景）
 *
 * 对照 W3D1 知识点：
 *   - 一次 recv() 不等于一个完整的 HTTP 请求
 *   - 追加到缓冲区 → 查找 \r\n\r\n → 解析 Content-Length → 读取请求体
 *   - 请求完整后再解析请求行、请求头和请求体
 */

#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stddef.h>   /* size_t */

#define HTTP_METHOD_LEN    16      /* 请求方法最大长度（GET / POST）    */
#define HTTP_PATH_LEN      256     /* 请求路径最大长度                  */
#define HTTP_VERSION_LEN   16      /* HTTP 版本最大长度                 */
#define HTTP_HEADER_COUNT  16      /* 最多解析的请求头数量              */
#define HTTP_HEADER_KEY_LEN   64   /* 请求头名称最大长度                */
#define HTTP_HEADER_VAL_LEN  256  /* 请求头值最大长度                  */
#define HTTP_BODY_MAX       8192  /* 请求体最大长度（POST 场景）       */

/*
 * 单个 HTTP 请求头键值对
 */
typedef struct {
    char key[HTTP_HEADER_KEY_LEN];
    char value[HTTP_HEADER_VAL_LEN];
} http_header_t;

/*
 * 解析后的 HTTP 请求
 */
typedef struct {
    char   method[HTTP_METHOD_LEN];          /* GET / POST 等             */
    char   path[HTTP_PATH_LEN];              /* / 或 /echo 等             */
    char   version[HTTP_VERSION_LEN];        /* HTTP/1.1                  */
    http_header_t headers[HTTP_HEADER_COUNT]; /* 请求头数组               */
    int    header_count;                     /* 实际请求头数量            */
    char   body[HTTP_BODY_MAX];             /* 请求体（POST 场景）        */
    int    body_len;                         /* 请求体实际长度            */
    int    content_length;                   /* Content-Length 头的值     */
} http_request_t;

/*
 * 解析 HTTP 请求。
 *
 * 从接收缓冲区中解析出 HTTP 请求的各个组成部分。
 * 调用前应确保缓冲区中已包含完整的 HTTP 请求
 * （即已通过 find_header_end() 确认 \r\n\r\n 存在）。
 *
 * 参数：
 *   buf      - 包含完整 HTTP 请求的接收缓冲区
 *   buf_len  - 缓冲区中的数据长度
 *   req      - 输出的解析后请求结构体
 *
 * 返回值：
 *    0  - 解析成功
 *   -1  - 解析失败（格式错误）
 */
int parse_http_request(const char *buf, int buf_len, http_request_t *req);

/*
 * 查找 HTTP 请求头结束标记 \r\n\r\n。
 *
 * 在缓冲区中查找 \r\n\r\n，返回其在缓冲区中的位置。
 *
 * 参数：
 *   buf     - 接收缓冲区
 *   buf_len - 缓冲区中的数据长度
 *
 * 返回值：
 *   >= 0  - \r\n\r\n 之后第一个字节的偏移（即请求体开始位置）
 *   -1    - 未找到（请求头不完整，需要继续接收）
 */
int find_header_end(const char *buf, int buf_len);

/*
 * 判断 HTTP 请求是否已完整接收。
 *
 * 需要同时满足：
 *   1. 已找到 \r\n\r\n（请求头完整）
 *   2. 如果存在 Content-Length，则请求体也已接收完整
 *
 * 参数：
 *   buf     - 接收缓冲区
 *   buf_len - 缓冲区中的数据长度
 *
 * 返回值：
 *   1  - 请求完整
 *   0  - 请求不完整（需要继续接收）
 */
int is_request_complete(const char *buf, int buf_len);

#endif /* HTTP_PARSER_H */
