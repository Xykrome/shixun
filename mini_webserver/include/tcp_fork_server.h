#ifndef TCP_FORK_SERVER_H
#define TCP_FORK_SERVER_H

#include "config.h"

/*
 * V0.7: 多进程 TCP 网络服务器（fork 并发版）
 *
 * 在 V0.6 单连接基础上引入多进程并发架构（W2D2 核心内容）：
 *   1. 父进程循环 accept() 等待客户端连接
 *   2. 每个连接 fork() 一个子进程独立处理 HTTP 请求
 *   3. 父进程捕获 SIGCHLD，使用 waitpid(-1, &stat, WNOHANG) 回收子进程
 *   4. accept() 被信号打断时判断 errno == EINTR，继续等待
 *   5. 忽略 SIGPIPE，防止客户端异常断开导致服务器终止
 *   6. 处理 MAX_CLIENTS 个连接后自动退出（方便自动测试）
 *
 * 参数：
 *   config  - 服务器配置（host/port 来自 conf/server.conf）
 *
 * 返回值：
 *    0      - 成功
 *   -1      - 失败
 */
int tcp_fork_server_run(const server_config_t *config);

#endif
