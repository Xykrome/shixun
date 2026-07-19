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
| Day12 | `make test11` | HTTP Static File Server V1.2 (W3D2) |
| Day13 | `make test12` | HTTP Search Server V1.3 (W3D3) |
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

## V1.2 架构 (W3D2)

```
epoll_wait → recv → 追加缓冲区 → 判断完整性(\r\n\r\n + Content-Length)
→ HTTP 解析 → GET → normalize_path() → realpath() 安全校验
→ stat() 文件元数据 → get_mime_type() 查 MIME 映射表
→ 发送响应头(Content-Type + Content-Length + Connection)
→ open() + 分块 read() + send_all() 发送文件 → close(file_fd)
→ access_log(含MIME) + log_info(含耗时) → epoll_ctl(DEL) + close(client_fd)
```

- 纯 epoll + 单线程事件循环（LT 模式 + EPOLLIN）
- 静态文件服务：URL 路径安全映射到 `www/` 目录，15 种 MIME 类型识别
- 路径安全：三层防护——`..` 检测 + `realpath()` 解析 + document root 边界验证
- 目录穿越 `/../etc/passwd` 返回 403，不存在文件返回 404，非 GET 方法返回 405
- 文件发送：8KB 固定缓冲区 + `send_all()` 可靠发送（处理部分 send）
- Content-Type 来自 MIME 映射表，Content-Length 来自 `st.st_size`（非 strlen）
- 访问日志增强：增加 MIME 类型字段；系统日志增加请求耗时（ms）
- 保留 V1.1 兼容：POST /echo 动态回显路由
- 请求计数达到 max_requests 后正常退出

## V1.3 架构 (W3D3)

```
epoll_wait → recv → 追加缓冲区 → 判断完整性(\r\n\r\n + Content-Length)
→ HTTP 解析 → route_request()
├─ GET /search        → handle_search_request() → 搜索表单 HTML
├─ GET /search?class.. → parse_query_string() → url_decode()
│                        → validate_class() / validate_keyword()
│                        → query_records(data/<class>.txt)
│                        → generate_result_page_html() → send_response()
├─ POST /search       → 检查 Content-Type(→415) + Content-Length(→413)
│                        → 解析请求体 → 同 GET 查询流程
├─ GET *              → serve_static_file() (V1.2)
└─ 其他               → 405 (Allow 头动态适配 /search 路径)
→ access_log(含MIME) + log_info(含耗时) → epoll_ctl(DEL) + close(client_fd)
```

- 纯 epoll + 单线程事件循环（LT 模式 + EPOLLIN）
- 动态查询：URL 解码 + 参数校验 + 数据文件 grep + HTML 转义防注入
- 数据文件：`data/<class>.txt`，制表符分隔，`strstr()` 匹配
- GET 参数在 URL 查询字符串，POST 参数在请求体
- 8 种状态码：200/400/403/404/405/413/415/500
- 错误页面中文提示："班级格式错误""班级数据不存在"等
- 保留 V1.2 静态文件服务和 V1.1 POST /echo 兼容
- 请求计数达到 max_requests 后正常退出
