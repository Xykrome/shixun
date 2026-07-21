# W3D5：配置文件与路由 WebServer V1.4：配置驱动

## 信息与软件工程学院 SCHOOL OF INFORMATION AND SOFTWARE ENGINEERING

### 从 V1.3 到 V1.4

V1.3 已能处理静态资源、GET 和 POST，但关键行为仍写死在代码中。

- 修改监听地址或端口，需要改代码并重新编译。
- 新增路由，需要继续增加 if/else。
- 日志路径和等级无法按环境调整。

V1.4：启动时读取配置，校验后构建服务器参数和路由表

## 本次实训目标

完成 WebServer V1.4 后，应能够：

- 使用成熟 JSON 库读取 server.json。
- 校验 host、port、document_root、日志和 routes。
- 根据 method + path 构建路由表并调用 handler。
- 正确返回 404、405，并保留 V1.3 的 GET、POST 与静态资源功能

## 配置描述数据，不执行代码

配置文件只保存需要按环境调整的数据：

- 监听地址、端口和网站根目录。
- 日志等级和日志文件。
- 路由的 method、path 与 handler 名称。

配置文件不保存函数地址或任意可执行代码。handler 名称必须在程序内注册表中查找，未知名称属于配置错误。

## 为什么本项目统一使用 JSON

格式没有绝对优劣。本实训统一使用 JSON，减少环境差异，把精力放在加载、校验和路由分发。

表格







|      格式      |                特点                |
| :------------: | :--------------------------------: |
| INI Properties |      键值直观，更偏 Java 生态      |
|      XML       |       表达能力强，但内容冗长       |
|      YAML      |          易读，但缩进敏感          |
|      JSON      | 结构清晰、解析库成熟 <- 本项目采用 |

## server.json 描述完整启动参数

配置文件描述服务器参数和路由元数据，不包含函数地址。下面字段贯穿后续实现与验收

json







```
{
  "server": {
    "host": "127.0.0.1",
    "port": 8080,
    "document_root": "./www"
  },
  "logging": {
    "level": "INFO",
    "file": "./logs/server.log"
  },
  "routes": [
    {"method":"GET", "path":"/users", "handler":"users_get"},
    {"method":"POST", "path":"/users", "handler":"users_create"}
  ]
}
```

## 先校验配置，再启动监听

所有配置必须在 bind ()、listen () 之前完成加载与校验，避免服务器带着错误状态运行

bash



运行





```
./mini_webserver ./config/server.json
```

流程：读取文件 -> JSON 解析 -> 字段校验

1. Config 对象
2. handler 映射
3. 路由表
4. create_listener(host, port)
5. 进入 epoll /kqueue 事件循环

## 配置校验要在启动阶段失败

必须校验：

- host 可绑定，port 范围为 1~65535。
- document_root 存在、可读且是目录。
- logging.level 只能取 DEBUG、INFO、WARN、ERROR。
- method 合法，path 以 / 开头，(method, path) 不重复。
- handler 名称已经在程序注册表中注册。

关键字段错误时：打印字段名和原因，服务器不进入监听状态。

## 路由键是 method + path

同一路径可以由不同 HTTP 方法执行不同逻辑；查询字符串不参与路由匹配

- GET /users?id=7、GET /users：路由键统一为 GET /users
- POST /users：路由键为 POST /users

匹配规则：

1. path 不存在 → 404
2. path 存在但 method 不允许 → 405 + Allow 响应头

映射示例：

- GET /users → users_get
- POST /users → users_create

## handler 名称通过注册表绑定函数

配置中的 handler 只是名称；程序通过注册表查找函数，未知名称则启动失败。

c



运行





```
typedef int (*Handler)(const HttpRequest *, HttpResponse *);

typedef struct {
    const char *name;
    Handler fn;
} HandlerEntry;

static const HandlerEntry registry[] = {
    {"users_get", users_get},
    {"users_create", users_create}
};

Handler find_handler(const char *name) {
    size_t n = sizeof registry / sizeof registry[0];
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(name, registry[i].name) == 0)
            return registry[i].fn;
    }
    return NULL;
}
```

## 请求分发只做一次明确决策

收到完整请求后，先规范化 path，再根据 method + path 查找路由并生成响应

c



运行





```
handle_request(req):
    path = normalize_path(req.target)
    route = find_route(req.method, path)
    if route exists:
        return route.handler(req)
    if method_not_allowed(path):
        return 405 + Allow header
    return try_static_file(path)
    return 404 Not Found
```

## V1.4 不能破坏 V1.3 的行为

V1.4 只改变 “参数和路由从哪里来”，已有 HTTP 行为必须保持一致。

- 配置成功：输出 host、port 和路由数量后进入事件循环。
- 目标存在且方法允许：返回 200 或相应业务结果。
- 没有动态路由且没有静态资源：返回 404。
- path 存在但 method 不允许：返回 405，并携带 Allow。
- 配置缺失、JSON 错误或字段非法：启动失败，不进入事件循环。

## 实训内容

在 WebServer V1.3 基础上，完成 WebServer V1.4。

1. 新建 config/server.json，服务器启动时读取并解析。
2. 配置文件至少包含：
   - 服务器绑定的 IP 和端口
   - www 静态资源根目录
   - 日志等级和日志路径
   - GET、POST 路由
3. 修改配置并重启后，新参数应生效。
4. 配置错误时停止启动并输出原因。
5. WebServer V1.3 原有功能继续可用。

## 最低验收证据

1. 修改配置中的 port，重启后监听新端口；源代码无需改动。
2. GET /users 与 POST /users 命中不同 handler。
3. 未知 path 返回 404；错误 method 返回 405 并包含 Allow。
4. 配置缺失、JSON 错误、重复路由或未知 handler 时拒绝启动，并打印原因。
5. 首页、静态资源及 V1.3 的 GET/POST 测试仍然通过。

## 总结

配置负责描述，注册表负责绑定，路由表负责分发



W3D5.HTTP安全认证20260722.pdf

PDF

提取 ppt 中的全部完整内容，md 格式

# W3D5 HTTP Basic 认证与受保护路由

## WebServer V1.5：在 V1.4 路由前增加认证

### 从 V1.4 到 V1.5：部分路由需要身份验证

V1.4 已完成配置加载和 method + path 路由，本次只增加一层认证检查。

- GET /：公开路由，任何客户端都可访问。
- GET /secured：受保护路由，Basic 凭据正确才执行 handler。
- 认证发生在路由匹配之后、handler 之前。
- epoll 事件循环和 HTTP 请求解析保持不变。

V1.5 的核心：同一台服务器，不同路由可以采用不同安全策略。

## 先区分认证与授权

1. 认证 Authentication

   ：确认 “你是谁”

   - 没有凭据或凭据错误 → 401 Unauthorized
   - 401 响应告诉客户端应采用哪种认证方案

2. 授权 Authorization

   ：确认 “你能做什么”

   - 身份已确认，但无权访问资源 → 403 Forbidden

3. 补充：路由不存在 → 404 Not Found，与身份是否正确无关

   

   本实验只实现认证，不实现角色或权限控制。

## 三种认证方案对比

表格







|     方案     |                           核心机制                           |
| :----------: | :----------------------------------------------------------: |
|    Basic     |   每次请求发送 Base64 (username:password)，服务端校验凭据    |
|   Session    |      客户端携带随机 SessionID；服务端保存并查询会话状态      |
| Bearer Token | 客户端发送`Authorization: Bearer <token>`，Token 可为不透明值或 JWT；Token≠JWT，不一定需要查询服务端 |

通用要求：生产环境必须使用 HTTPS。本次必做仅实现 Basic 认证。

## Basic 认证是一次 401 挑战

Basic 不保存会话，客户端访问受保护路由时**每次请求**都携带 Authorization 请求头。

交互流程：

1. 客户端 → GET /secured（无 Authorization 头）

2. 服务端 ← HTTP/1.1 401 Unauthorized

   plaintext

   

   

   

   ```
   WWW-Authenticate: Basic realm="mini_webserver"
   ```

3. 客户端 → GET /secured

   plaintext

   

   

   

   ```
   Authorization: Basic <Base64(username:password)>
   ```

4. 服务端响应：

   - 凭据正确：200 OK
   - 凭据错误：401 Unauthorized（非 403）

## Authorization 头解析校验逻辑

服务器仅对配置`auth=basic`的路由执行校验：

1. 读取唯一的 Authorization 请求头
2. 校验认证方案为 Basic，提取 Base64 编码字符串
3. Base64 解码，按第一个冒号分割为 username:password
4. 和服务端存储的凭据比对

错误判定规则：

- 缺少头、Scheme 错误、Base64 格式非法、无冒号分隔、账号密码错误 → 返回 401
- Authorization 头重复、请求头超长 → 返回 400
- 凭据校验通过 → 正常执行路由 handler
- 强制规范：日志禁止打印 Authorization、Base64 编码串、明文密码

## 认证执行流程（中间件逻辑）

完整请求处理链路：

1. 解析 HTTP 请求，通过 method + path 匹配路由

   - 无匹配路由 → 直接返回 404 / 405

   - 匹配到路由：判断路由配置

     ```
     route.auth == "basic"
     ```

     1. 若未开启 basic 认证：直接执行 handler (req)

     2. 若开启 basic 认证：执行

        ```
        validate_basic(req)
        ```

        - 校验通过 → 执行 handler (req)
        - 校验失败 → 返回 401，携带 WWW-Authenticate 响应头

### 路由配置示例

json







```
{"method":"GET", "path":"/secured", "handler":"secured_get", "auth":"basic"}
```

## Basic 认证安全边界与规范

1. Base64 仅为编码，无加密效果，抓包可直接还原账号密码；**生产环境必须搭配 HTTPS**，防止窃听、篡改
2. 账号密码禁止硬编码在源码、禁止写入访问日志
3. 正式系统存储密码哈希，校验时规避时序泄露风险
4. 受保护资源响应建议增加`Cache-Control: no-store`，避免浏览器缓存敏感页面
5. 限制请求头最大长度，认证失败可添加限流，降低暴力破解风险

实验约束：仅在[localhost](https://link.wtturl.cn/?target=https%3A%2F%2Flocalhost&scene=im&aid=497858&lang=zh)演示协议流程，示例账号密码不可用于线上真实系统。

## 训练内容：实现 HTTP Basic 认证

基于 WebServer V1.4 新增 Basic 认证，覆盖 3 类访问场景：

1. 访问公开页面
   - 请求地址：`http://localhost:8080/`
   - 预期结果：`HTTP/1.1 200 OK`，正常展示首页
2. 无认证凭据访问受保护页面
   - 请求地址：`http://localhost:8080/secured`
   - 预期结果：`HTTP/1.1 401 Unauthorized`，携带`WWW-Authenticate`响应头
3. 使用正确账号密码访问受保护页面
   - 账号：student，密码：lab123
   - 请求地址：`http://localhost:8080/secured`
   - 预期结果：`HTTP/1.1 200 OK`，返回受保护页面内容

补充硬性要求：

- 账号 / 密码错误统一返回 401，不返回 403
- V1.4 全部原有功能保持可用
- 日志严禁输出任何认证相关凭据

## 最低验收证据

测试命令示例：

bash



运行





```
BASE=http://127.0.0.1:9090
curl -i "$BASE/"
curl -i "$BASE/secured"
curl -i -u student:wrong "$BASE/secured"
curl -i -u student:lab123 "$BASE/secured"
```

预期校验点：

1. 访问 `/` 返回 200；无凭据访问 `/secured` 返回 401，携带`WWW-Authenticate`头
2. 错误账号密码返回 401；正确凭据返回 200
3. 日志中不存在 Authorization 头、Base64 编码串、明文密码
4. V1.4 原有功能全部正常：静态资源、404/405、配置加载、文件描述符清理等测试用例均可通过

## 扩展训练（选做）

### 选做 1：Session 会话认证

1. 使用不可预测随机 SessionID；登录后刷新 SessionID，登出 / 过期时服务端销毁会话
2. Cookie 配置：HttpOnly、Secure、SameSite，增加 CSRF 防护
3. 安全风险说明：SessionID 泄露会引发会话劫持；CSRF 根源为浏览器自动携带 Cookie

### 选做 2：Bearer Token / JWT

1. 使用`Authorization: Bearer <token>`，依赖成熟第三方认证库
2. JWT 强制校验：签名、exp 过期时间、iss 签发者、aud 受众；负载禁止存储密码
3. 不透明 Token 仍需服务端校验状态

补充说明：两项扩展均为选做，不替代 Basic 认证必做需求；禁止自行实现加密算法。