/*
 * W2D5 epoll_server.h — epoll HTTP 服务器头文件 (Webserver V1.0)
 *
 * 功能：
 *   使用 epoll() I/O 多路复用技术实现一个具备高并发连接管理
 *   基础的 webserver 服务器。
 *
 * 对照 W2D5 知识点：
 *   - epoll_create1() 创建 epoll 实例
 *   - epoll_ctl() 注册/修改/删除监听事件
 *   - epoll_wait() 等待就绪事件
 *   - LT（Level-Triggered）模式 + EPOLLIN
 *   - HTTP 请求解析与响应生成
 *   - 路由分发：/hello、/users/<name>、404
 */

#ifndef EPOLL_SERVER_H
#define EPOLL_SERVER_H

#define MAX_CLIENTS      64          /* 最大客户端数量           */
#define RECV_BUF_SIZE    65536       /* HTTP 请求接收缓冲区大小   */
#define RESP_BUF_SIZE    65536       /* HTTP 响应发送缓冲区大小   */
#define DEFAULT_PORT     8080        /* 默认监听端口（HTTP）      */
#define BACKLOG          10          /* 已完成连接队列最大长度    */
#define MAX_EVENTS       64          /* epoll_wait 最大事件数    */

/*
 * 客户端连接信息
 */
#ifndef CLIENT_INFO_T_DEFINED
#define CLIENT_INFO_T_DEFINED
typedef struct {
    int   fd;                          /* 客户端套接字，-1 = 空闲 */
    char  ip[64];                      /* 客户端 IP 地址          */
    int   port;                        /* 客户端端口号            */
    char  recv_buf[RECV_BUF_SIZE];     /* HTTP 请求接收缓冲区      */
    int   buf_len;                     /* 缓冲区已用长度           */
} client_info_t;
#endif

/*
 * 启动 epoll HTTP 服务器 (Webserver V1.0)
 *
 * 参数：
 *   port         - 监听端口号
 *   max_requests - 最大请求处理数（达到后正常退出）
 *
 * 返回值：
 *    0  - 正常退出（达到 max_requests）
 *   -1  - 出错
 */
int epoll_server_run(int port, int max_requests);

#endif /* EPOLL_SERVER_H */
