/*
 * W3D4 http_server.h — 配置驱动 HTTP 服务器 V1.4 头文件
 *
 * 功能：
 *   结合 epoll 事件循环 + 静态文件服务 + 动态路由分发：
 *   - 启动时读取 server.json 构建路由表
 *   - 请求到达后按 method+path 查找路由 → 调用 handler
 *   - 无匹配路由时回退到静态文件服务
 *   - 保留 V1.3 全部 HTTP 行为
 *
 * V1.4 变更：host/port/document_root/log/routes 全部来自配置文件，
 * 不再硬编码在代码中。
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "config.h"    /* server_config_t */

#define MAX_CLIENTS      64          /* 最大客户端数量             */
#define RECV_BUF_SIZE    65536       /* 接收缓冲区大小             */
#define RESP_BUF_SIZE    65536       /* 响应缓冲区大小             */
#define BACKLOG          10          /* 已完成连接队列最大长度      */
#define MAX_EVENTS       64          /* epoll_wait 最大事件数      */

/*
 * 客户端连接信息
 */
#ifndef CLIENT_INFO_T_DEFINED
#define CLIENT_INFO_T_DEFINED
typedef struct {
    int   fd;
    char  ip[64];
    int   port;
    char  recv_buf[RECV_BUF_SIZE];
    int   buf_len;
} client_info_t;
#endif

/*
 * 启动基于 epoll 的配置驱动 HTTP 服务器 (W3D4 V1.4)
 *
 * 参数来自 server_config_t：
 *   - host, port        → bind() 地址
 *   - document_root     → 静态文件根目录
 *   - log_level, log_file → 日志配置
 *   - routes[]          → 动态路由表
 *
 * 参数：
 *   config       - 已加载并校验的服务器配置
 *   max_requests - 最大请求处理数（达到后正常退出）
 *
 * 返回值：
 *    0  - 正常退出
 *   -1  - 出错
 */
int http_server_run(const server_config_t *config, int max_requests);

/*
 * 按名称查找已注册的 handler 函数（用于配置校验和路由构建）。
 * 返回值：找到则返回函数指针，否则返回 NULL。
 */
typedef int (*Handler)(int client_fd, const void *req,
                       int *status_code, const char **mime_type,
                       int *body_bytes);

Handler find_handler_by_name(const char *name);

#endif /* HTTP_SERVER_H */
