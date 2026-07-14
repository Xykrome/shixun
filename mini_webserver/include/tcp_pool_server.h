#ifndef TCP_POOL_SERVER_H
#define TCP_POOL_SERVER_H

#include "config.h"

/*
 * V0.8: 线程池 TCP 网络服务器
 *
 * 在 V0.7 多进程版本基础上改为线程池架构（W2D3 核心内容）：
 *   1. socket/bind/listen 创建 TCP 监听套接字
 *   2. 预创建固定数量的 worker 线程（线程池）
 *   3. 主线程循环 accept()，将 client_fd 放入任务队列
 *   4. worker 线程从队列取 client_fd，处理 HTTP 请求，发送响应，关闭连接
 *   5. 达到 max_clients 后停止 accept，关闭线程池，等待所有 worker 退出
 *
 * V0.7 vs V0.8 对比：
 *   V0.7: fork 子进程处理每个连接（进程创建/销毁开销大）
 *   V0.8: 固定数量的 worker 线程复用（减少创建/销毁开销，控制并发数）
 *
 * 参数：
 *   config     - 服务器配置（host/port 来自 conf/server.conf）
 *   num_workers - 线程池大小（worker 线程数量）
 *
 * 返回值：
 *    0      - 成功
 *   -1      - 失败
 */
int tcp_pool_server_run(const server_config_t *config, int num_workers);

#endif
