/*
 * W3D2 http_server.h — 基于 epoll 的 HTTP 静态文件服务器 V1.2 头文件
 *
 * 功能：
 *   结合 W2D5 的 epoll 事件循环和 W3D2 的静态文件服务，
 *   实现一个支持静态文件响应、MIME 识别、路径安全和日志记录的 web 服务器。
 *
 * 对照 W3D2 知识点：
 *   - epoll_wait 发现事件 → recv 追加缓冲区 → 判断 HTTP 完整性
 *   - 解析请求行、请求头和请求体 → 静态文件路由分发 → 构造响应
 *   - 路径规范化 + realpath() 安全校验 → stat() 文件元数据
 *   - MIME 映射 + 分块 read()+send_all() → 记录访问日志 + 系统日志
 *   - GET * → 静态文件服务, POST /echo → 动态回显 (V1.1 兼容)
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#define MAX_CLIENTS      64          /* 最大客户端数量             */
#define RECV_BUF_SIZE    65536       /* 接收缓冲区大小             */
#define RESP_BUF_SIZE    65536       /* 响应缓冲区大小             */
#define DEFAULT_PORT     8080        /* 默认监听端口（HTTP）       */
#define BACKLOG          10          /* 已完成连接队列最大长度      */
#define MAX_EVENTS       64          /* epoll_wait 最大事件数      */

/*
 * 客户端连接信息
 * 注意：如果已通过 epoll_server.h 定义，则跳过来避免类型冲突
 */
#ifndef CLIENT_INFO_T_DEFINED
#define CLIENT_INFO_T_DEFINED
typedef struct {
    int   fd;                          /* 客户端套接字，-1 = 空闲   */
    char  ip[64];                      /* 客户端 IP 地址            */
    int   port;                        /* 客户端端口号              */
    char  recv_buf[RECV_BUF_SIZE];     /* HTTP 请求接收缓冲区       */
    int   buf_len;                     /* 缓冲区已用长度            */
} client_info_t;
#endif

/*
 * 启动基于 epoll 的 HTTP 静态文件服务器 (W3D2 V1.2)
 *
 * 支持路由：
 *   GET *       → 静态文件服务（www/ 目录，MIME 识别，分块发送）
 *   POST /echo  → HTTP/1.1 200 OK + 回显请求体（V1.1 兼容）
 *   其他方法     → 405 Method Not Allowed
 *
 * 参数：
 *   port         - 监听端口号
 *   max_requests - 最大请求处理数（达到后正常退出）
 *
 * 返回值：
 *    0  - 正常退出（达到 max_requests）
 *   -1  - 出错
 */
int http_server_run(int port, int max_requests);

#endif /* HTTP_SERVER_H */
