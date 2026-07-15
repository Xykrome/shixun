/*
 * W2D4 select_server.h — select 多客户端聊天服务器头文件
 *
 * 功能：
 *   使用 select() I/O 多路复用技术实现一个能够同时处理
 *   多个客户端的聊天服务器。
 *
 * 对照 W2D4 知识点：
 *   - select() 的 I/O 多路复用模型
 *   - FD_ZERO / FD_SET / FD_ISSET / FD_CLR 宏操作
 *   - 非阻塞 accept 处理多个客户端连接
 *   - 基于 \n 的消息分帧（处理粘包/半包问题）
 *   - 客户端生命周期管理（连接/消息/quit/断开）
 */

#ifndef SELECT_SERVER_H
#define SELECT_SERVER_H

#define MAX_CLIENTS      64          /* 最大客户端数量           */
#define RECV_BUF_SIZE    4096        /* 单个客户端接收缓冲区大小  */
#define MSG_BUF_SIZE     4096        /* 消息组装缓冲区大小        */
#define DEFAULT_PORT     8888        /* 默认监听端口             */
#define BACKLOG          10          /* 已完成连接队列最大长度    */

/*
 * 客户端连接信息
 *
 * fd       - 客户端套接字描述符，-1 表示该槽位空闲
 * ip       - 客户端 IP 地址字符串（如 "127.0.0.1"）
 * port     - 客户端端口号（网络字节序已转回主机字节序）
 * recv_buf - 接收缓冲区（累积不完整的消息）
 * buf_len  - 接收缓冲区中已累积的字节数
 */
typedef struct {
    int   fd;                          /* 客户端套接字，-1 = 空闲 */
    char  ip[64];                      /* 客户端 IP 地址          */
    int   port;                        /* 客户端端口号            */
    char  recv_buf[RECV_BUF_SIZE];     /* 接收缓冲区              */
    int   buf_len;                     /* 缓冲区已用长度          */
} client_info_t;

/*
 * 启动 select 聊天服务器
 *
 * 参数：
 *   port - 监听端口号（0 表示使用 DEFAULT_PORT）
 *
 * 返回值：
 *    0  - 正常退出
 *   -1  - 出错
 */
int select_server_run(int port);

#endif /* SELECT_SERVER_H */
