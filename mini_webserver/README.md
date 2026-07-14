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
| 全部 | `make test-all` | 运行所有测试 |
