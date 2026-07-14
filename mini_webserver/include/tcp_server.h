#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "config.h"

/*
 * V0.6: TCP 网络服务器
 *
 * 使用 socket/bind/listen/accept 创建真实的 TCP 服务器，
 * 接收客户端（curl/浏览器）发来的 HTTP 请求，处理后返回 HTTP 响应。
 * 处理完一个连接后正常退出。
 *
 * 参数：
 *   config  - 服务器配置（host/port 来自 conf/server.conf）
 *
 * 返回值：
 *    0      - 成功
 *   -1      - 失败
 */
int tcp_server_run(const server_config_t *config);

#endif
