# Mini Web Server

Linux 环境下基于 C 语言的 HTTP 服务器，支持五种并发模型。

## 版本演进

| 版本 | 模式 | 命令 |
|------|------|------|
| V0.4 | 多进程（fork） | `./mini_web_server conf/server.conf` |
| V0.5 | 多线程（pthread） | `./mini_web_server --thread conf/server.conf [N]` |
| V0.6 | TCP 单连接 | `./mini_web_server --tcp conf/server.conf` |
| V0.7 | TCP 多进程 | `./mini_web_server --fork conf/server.conf` |
| V0.8 | TCP 线程池 | `./mini_web_server --pool conf/server.conf [N]` |
| V1.0 | Epoll 事件驱动 | `./mini_web_server serve-epoll <max_requests>` |
| V1.1 | Epoll HTTP 服务器 | `./mini_web_server serve-http <max_requests>` |

## 目录结构

```
mini_webserver/
├── conf/server.conf          # 服务器配置
├── data/users.csv            # 用户数据
├── include/                  # 头文件
│   ├── thread_pool.h         # V0.8 线程池
│   ├── tcp_pool_server.h     # V0.8 TCP 线程池服务器
│   └── ...
├── src/                      # 源文件
│   ├── thread_pool.c         # V0.8 线程池实现
│   ├── tcp_pool_server.c     # V0.8 TCP 线程池服务器
│   ├── epoll_server.c         # V1.0 epoll HTTP 服务器
│   ├── http_parser.c           # W3D1 HTTP 请求解析器
│   ├── http_server.c           # V1.1 epoll HTTP 服务器 (W3D1)
│   ├── main.c                # 主入口
│   └── ...
├── tests/                    # 测试脚本
│   ├── test_day08.sh         # V0.8 测试
│   └── ...
├── Makefile
└── README.md
```

## 编译与运行

```bash
# 编译
make

# 启动 V0.8 线程池服务器（默认 4 个 worker）
./mini_web_server --pool conf/server.conf

# 自定义 worker 数量
./mini_web_server --pool conf/server.conf 8

# 运行测试
make test8
```

## V0.8 架构

```
listen_fd → accept() → client_fd → task queue → worker → handler → close(client_fd)
```

- 主线程负责 socket/bind/listen/accept
- 预创建固定数量 worker 线程（线程池）
- client_fd 放入数组环形队列（`work_t`）
- 互斥量 + 条件变量实现生产者-消费者同步
- 队列空时 worker 阻塞于 `pthread_cond_wait`，不忙等
- 达到 `max_clients` 后停止 accept，关闭线程池

## 测试

| 测试 | 命令 | 内容 |
|------|------|------|
| Day01 | `make test` | 基础功能 / 配置加载 |
| Day05 | `make test5` | 多线程请求处理 |
| Day07 | `make test7` | 多进程 TCP 服务器 |
| Day08 | `make test8` | 线程池 TCP 服务器 |
| Day10 | `make test9` | epoll Webserver V1.0 |
| Day11 | `make test10` | HTTP Server V1.1 (W3D1) |
| 全部 | `make test-all` | 运行所有测试 |

## V1.0 架构

```
listen_fd → epoll_create1() → epoll_wait() → accept/recv → HTTP handler → send → epoll_ctl(DEL) + close
```

- 纯 epoll + 单线程事件循环（LT 模式 + EPOLLIN）
- 未使用 select、多线程、多进程、线程池
- 支持 /hello、/users/<name> 路由，未知路径返回 404
- 请求计数达到 max_requests 后正常退出

## V1.1 架构 (W3D1)

```
epoll_wait → recv → 追加缓冲区 → 判断完整性(\r\n\r\n + Content-Length)
→ HTTP 解析(请求行/头/体) → 路由分发 → send → 系统日志 + 访问日志
→ epoll_ctl(DEL) + close
```

- 纯 epoll + 单线程事件循环（LT 模式 + EPOLLIN）
- 缓冲区追加模式：一次 recv() ≠ 一个完整 HTTP 请求
- 支持 GET /（200 + HTML）、GET /missing（404）、POST /echo（200 + 回显）
- 系统日志 + 访问日志分离，支持 DEBUG/INFO/WARNING/ERROR 四级
- 正确设置 Content-Type、Content-Length、Connection 响应头
- 请求计数达到 max_requests 后正常退出
