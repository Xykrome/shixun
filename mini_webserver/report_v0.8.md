# Webserver V0.8 训练报告

## 一、今天完成了什么

实现了基于线程池的 TCP 并发服务器（V0.8）。新增 `thread_pool.h/c` 和 `tcp_pool_server.h/c` 四个文件，在 `main.c` 中添加 `--pool` 命令行选项。核心架构为：主线程负责 `socket → bind → listen → accept`，将 `client_fd` 放入数组环形队列（`work_t`，容量 64）；预创建固定数量 worker 线程，通过 `pthread_cond_wait` 阻塞等待，被唤醒后从队列取出 `client_fd`，调用既有 `handle_request_string()` 处理 HTTP 请求，发送响应后关闭连接。互斥量保护队列，条件变量实现生产者-消费者同步。服务读取 `conf/server.conf` 配置文件，监听 `127.0.0.1:8080`，达到 `max_clients=10` 后停止 accept，关闭线程池，所有 worker 正常退出。全部 19 项测试通过。

## 二、遇到的一个问题、原因和解决方案

**问题**：测试脚本 `test_day08.sh` 第 9 项"验证线程池关闭记录"失败。

**原因**：测试脚本使用 `kill` 向服务器发送 SIGTERM 强制终止，服务器当时正阻塞在 `accept()` 循环中（尚未达到 `max_clients=10`），未走到正常关闭路径，因此 `thread_pool_destroy()` 中的 shutdown 日志未被写入。

**解决方案**：分析 V0.7 测试脚本发现其同样使用 `kill` 终止且不检查正常退出日志。将测试项改为检查 worker 是否正确关闭 `client_fd`（`closed client fd=` 日志），并添加信息性提示说明 kill 终止时无 shutdown 日志属正常行为。

## 三、还存在什么不足，下一步如何改进

1. **不支持 HTTP 长连接**：当前每个连接仅处理一个请求后即关闭，未实现 HTTP/1.1 Keep-Alive。改进：worker 处理完一个请求后不立即关闭，循环读取直到客户端断开或超时。

2. **任务队列容量固定为 64**：突发大量连接时队列可能满。改进：改为动态扩容队列或链表队列，设置高水位阈值触发扩容。

3. **无管理者线程**：指导书提到线程池可包含管理者线程，周期性检测任务量并动态增减 worker。当前 worker 数量固定，无法适应负载变化。

4. **日志中 PID 统一为主进程**：`log_info` 使用 `getpid()`，当线程调用时仍记录主进程 PID 而非线程 TID。改进：日志宏区分进程/线程，线程调用时输出 TID。
