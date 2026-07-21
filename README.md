# shixun — 信息与软件工程学院 实训项目

电子科技大学 信息与软件工程学院 2024-2025 学年实训代码仓库。

## 项目结构

```
shixun/
├── day1~7/            # W1D1 ~ W2D2: C语言基础、进程、线程、网络编程
├── select_chat/       # W2D4: select 多客户端聊天室
├── epoll_chat/        # W2D5: epoll 多客户端聊天室
├── mini_webserver/    # W2D5~W3D5: mini_webserver 主线项目
├── 报告初稿/           # Day3~Day15 训练报告
└── 指导书/             # W3D4/W3D5 实训指导书
```

## mini_webserver 演进路线

| 版本 | 日期 | 标签 | 核心能力 |
|------|------|------|---------|
| V0.4 | Day4 | 多进程 | `fork()` 并发处理请求 |
| V0.5 | Day5 | 多线程 | `pthread` 并发处理请求 |
| V0.6 | Day6 | TCP 单连接 | `socket/bind/listen/accept` |
| V0.7 | Day7 | TCP 多进程 | `fork()` + TCP |
| V0.8 | Day8 | TCP 线程池 | 线程池 + TCP |
| V1.0 | Day10 | W2D5 | epoll 事件循环 + HTTP 基础 |
| V1.1 | Day11 | W3D1 | HTTP 请求解析 + 路由分发 |
| V1.2 | Day12 | W3D2 | 静态文件服务 + MIME + 路径安全 |
| V1.3 | Day13 | W3D3 | GET/POST 动态查询 + 学生信息搜索 |
| V1.4 | Day14 | W3D4 | JSON 配置驱动 + cJSON + 11项校验 + 路由表 |
| **V1.5** | **Day15** | **W3D5** | **HTTP Basic 认证 + Session 会话认证 + Bearer Token 认证** |

## V1.5 当前状态

### 三种认证方案，按路由独立配置

```json
// 示例：config/server.json
{"method":"GET", "path":"/secured",   "handler":"secured_get",       "auth":"basic"},
{"method":"GET", "path":"/dashboard", "handler":"session_dashboard", "auth":"session"},
{"method":"GET", "path":"/api/me",    "handler":"api_me",            "auth":"bearer"}
```

| 方案 | auth 值 | 凭据传输 | 适用场景 |
|------|---------|---------|---------|
| HTTP Basic | `"basic"` | `Authorization: Basic <Base64>` 头 | 浏览器原生弹窗 |
| Session | `"session"` | `Cookie: session_id=<random>` + CSRF Token | 传统 Web 应用 |
| Bearer Token | `"bearer"` | `Authorization: Bearer <token>` 头 | API / 移动端 |

### 认证中间件架构

```
请求 → find_route(method, path) → 匹配路由
  → route.auth == "basic"   → validate_basic_auth()   → 失败→401
  → route.auth == "session" → validate_session_auth() → 失败→401/403
  → route.auth == "bearer"  → validate_bearer_auth()  → 失败→401
  → 通过 → handler(req)
```

### 完整路由表 (10条)

| 方法 | 路径 | 认证 | 功能 |
|------|------|:--:|------|
| GET | `/` | — | 静态首页 (V1.5) |
| GET | `/search` | — | 学生信息查询 |
| POST | `/search` | — | 学生信息查询 |
| POST | `/echo` | — | 请求回显 |
| GET | `/secured` | basic | Basic 认证演示 |
| GET | `/login` | — | 登录页面 |
| POST | `/login` | — | Session 登录 |
| POST | `/logout` | — | 销毁 Session |
| GET | `/dashboard` | session | 个人中心 |
| POST | `/token` | — | 获取 Bearer Token |
| GET | `/api/me` | bearer | Token 保护的 API |

### 安全特性

- Base64 凭据解码 + 校验（不依赖外部加密库）
- SessionID 从 `/dev/urandom` 生成 64 位十六进制随机值
- Cookie 设置 `HttpOnly; SameSite=Strict` 属性
- CSRF Token 双重校验（请求头 + POST body）
- 登出清除 Cookie 含 `Max-Age=0` + `Expires` 过期时间
- Bearer Token 支持 `iss`/`aud`/`exp` 标准字段
- 日志严禁输出 Authorization 头、Base64 编码串、明文密码

## 构建与运行

```bash
cd mini_webserver
make
./mini_web_server config/server.json          # 不限请求数
./mini_web_server config/server.json 10       # 处理10个请求后退出
```

## 测试

```bash
make test13   # Day14 V1.4 配置驱动: 32/32 ✓
make test14   # Day15 V1.5 Basic认证: 28/28 ✓
```

## 许可证

本项目为电子科技大学信息与软件工程学院实训项目，仅供学习参考。
