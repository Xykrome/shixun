# W3D3：HTTP处理GET和POST请求

## mini_webserver V1.3：从静态资源到动态查询

------

# V1.3只增加动态请求层

V1.2已经具备：

- 静态资源 GET
- epoll / kqueue 事件循环
- 访问日志
- fd 清理

V1.3继续增加：

- 查询参数
- POST 请求体
- 动态 HTML 响应

事件循环不重写，HTTP公共解析继续复用。

请求完整后：

```
method + target + headers + body
        ↓
route_request()
├─ 静态资源 → serve_static_file()
└─ /search → GET / POST 查询处理
```

------

# 实训目标：让同一个查询支持 GET 和 POST

每名学生独立在 V1.2 基础上升级，完成 `/search` 动态查询路由。

## 必须完成

1. GET `/search` 返回查询表单
2. GET `/search?class=2011&keyword=男` 返回结果
3. POST `/search` 解析表单请求体并返回相同结果
4. 支持 UTF-8、中文查询、错误响应、日志和统一清理

最终成果：

浏览器表单、curl 命令和日志可以相互验证。

------

# GET 和 POST 的核心差异是参数位置与语义

两者都能提交参数。

POST 不会因为参数在请求体中就自动更安全，HTTPS 才提供传输加密。

| 项目       | GET            | POST                 |
| ---------- | -------------- | -------------------- |
| 参数位置   | URL查询字符串  | 请求体               |
| 典型用途   | 读取、查询     | 提交表单或待处理数据 |
| HTTP语义   | 安全方法、幂等 | 不要求幂等           |
| 收藏与缓存 | 通常可以       | 通常不用于收藏       |

本项目：

GET 和 POST 查询同一份数据，返回相同结果。

> “安全方法”表示不改变服务器状态，不表示传输内容已加密。

------

# 先判断请求完整，再进入业务处理

TCP 是字节流：

一次 `recv()` 可能只有半个请求。

每个 client_fd 都要维护独立缓冲区。

```
请求行 + 请求头 + 可选请求体

1. 缓冲区出现 \r\n\r\n
   → 请求头完整

2. GET 通常没有请求体
   → 可以解析和路由

3. POST 读取 Content-Length

4. 已收请求体字节数 >= Content-Length
   → 整个请求完整

5. 未完整
   → 保留缓冲区
   → 等待下一次 EPOLLIN / EVFILT_READ
```

不能把一次 recv() 当成一条 HTTP 请求。

------

# GET 参数位于请求目标的查询字符串

请求路径与查询参数必须分开处理。

参数名统一使用：

- class
- keyword

示例：

```
GET /search?class=2011&keyword=%E7%94%B7 HTTP/1.1
Host: 127.0.0.1:9001
Connection: close
```

解析结果：

```
path = /search

query = class=2011&keyword=%E7%94%B7

class = 2011

keyword = 男
```

规则：

```
? 分隔路径和查询串
& 分隔参数
= 分隔名称和值
```

------

# URL解码后再验证参数

```
%HH
```

表示一个编码字节。

表单编码中的：

```
+
```

表示空格。

中文按 UTF-8 字节进行百分号编码。

## 处理顺序

```
原始参数
    ↓
URL解码
    ↓
UTF-8校验
    ↓
长度/格式校验
    ↓
业务使用
```

### class

只允许：

```
4位数字
```

例如：

```
2011
```

### keyword

要求：

```
1～64字节
```

拒绝：

- 控制字符

禁止：

```
..
/
\
空字节
任意文件名
```

class 映射为：

```
data/2011.txt
```

最终路径必须仍位于 data 根目录内。

输出到 HTML 前还要转义：

```
&
<
>
"
'
```

防止页面内容被注入。

------

# POST表单数据位于请求体

浏览器表单使用：

```
application/x-www-form-urlencoded
```

input 的 name 必须与服务器参数名一致。

```
<form action="/search"
      method="POST"
      accept-charset="UTF-8">

    <input name="class">
    <input name="keyword">

    <button type="submit">
        查询
    </button>

</form>
```

请求示例：

```
POST /search HTTP/1.1
Host: 127.0.0.1:9001

Content-Type:
application/x-www-form-urlencoded

Content-Length: 28

Connection: close

class=2011&keyword=%E7%94%B7
```

------

# POST必须按Content-Length继续接收

找到请求头结束位置后：

不能立刻处理 POST。

必须确认请求体字节数达到 Content-Length。

```
recv()
    ↓
追加到客户端缓冲区

找到 \r\n\r\n
    ↓
得到 header_end

解析 Content-Length
解析 Content-Type
    ↓

received_body < Content-Length
    ↓
等待下一次可读事件

received_body >= Content-Length
    ↓
解析表单并路由
```

错误处理：

- 缺少或非法 Content-Length → 400
- 请求体超过 4096 字节 → 413
- Content-Type 不正确 → 415

------

# 路由决定由哪个处理函数接管请求

先匹配：

- 方法
- 路径

然后调用对应业务函数。

静态资源仍交给 V1.2 处理。

```
GET /search
    → 返回查询表单

GET /search?class=...&keyword=...
    → 解析URL参数并查询

POST /search
    → 解析请求体并查询

GET /css/...
GET /img/...
    → serve_static_file()

未知路径
    → 404 Not Found

/search上的其他方法
    → 405
    → Allow: GET, POST
```

建议入口：

```
route_request(client_fd, request)
```

------

# 查询数据前先限制文件和输入边界

业务参数是：

```
班级编号
```

不是任意文件名。

服务器只访问：

```
data/
```

目录中的普通文件。

数据文件：

```
data/2011.txt
```

记录格式：

```
学号\t姓名\t性别
```

一行一条记录。

规则：

```
class=2011
    ↓
映射到 data/2011.txt

keyword
    ↓
在任一字段中匹配

匹配成功
    ↓
返回记录
```

错误情况：

```
无匹配记录
    → 200
    → 显示“未找到结果”

班级格式非法
    → 400

数据文件不存在
    → 404
```

查询结果写入 HTML 前必须转义。

不能直接拼接不可信输入。

------

# 动态响应必须先生成正文，再计算字节数

查询结果不是固定 HTML 文件。

而是服务器动态生成。

```
HTTP/1.1 200 OK
Content-Type: text/html; charset=utf-8
Content-Length: <body_bytes>
Connection: close

<html>...查询结果...</html>
```

流程：

1. 先生成完整 UTF-8 正文
2. Content-Length 使用正文字节数
3. send_all() 发送响应头和正文
4. 响应完成后统一删除事件并关闭 client_fd

------

# 状态码、日志和fd清理必须表达同一结果

正常访问错误：

```
access.log
```

内部 I/O 异常：

```
system.log
```

所有分支进入同一个清理出口。

## 状态码

| 状态码 | 含义                 |
| ------ | -------------------- |
| 200    | 查询或表单成功       |
| 400    | 请求或参数格式错误   |
| 403    | 路径越界或目标不允许 |
| 404    | 路由或数据文件不存在 |
| 405    | 方法不允许           |
| 413    | 请求体过大           |
| 415    | 不支持的Content-Type |
| 500    | 服务器内部错误       |

### access.log

记录：

- 客户端
- 方法
- 路径
- 状态
- 字节数
- 耗时

### system.log

记录：

- 启动
- 退出
- open 失败
- read 失败
- send 失败

统一清理流程：

```
响应
 ↓
写日志
 ↓
删除epoll事件
 ↓
close(client_fd)
 ↓
释放缓冲区
```

------

# 按可运行的小步骤完成 V1.3

不要等所有代码写完再调试。

每完成一步：

```
编译
curl测试
```

步骤：

1. 复用 V1.2 事件循环
2. 完成 GET /search
3. 增加 query string 解析
4. 增加 URL 解码和参数校验
5. 完成数据文件查询
6. 完成 HTML 结果页
7. 增加 POST 头部处理
8. 增加 Content-Length 和请求体累计
9. POST 复用 query_records()
10. 补齐错误响应
11. 实现统一清理
12. 完成中文测试
13. 完成分段请求测试
14. 完成三客户端测试
15. 完成日志测试

------

# 实训内容：完成 mini_webserver V1.3

每名学生独立在 V1.2 基础上完成 GET 和 POST 动态查询。

IP 和端口由命令行指定。

## 具体实现要求

1. 保留 V1.2 的 epoll 事件循环、静态资源处理、日志和统一 fd 清理。
2. 实现 GET `/search` 返回查询表单；GET 查询参数统一使用 `class` 和 `keyword`。
3. 实现 POST `/search`，解析 `application/x-www-form-urlencoded` 请求体。
4. 根据 `\r\n\r\n` 和 `Content-Length` 累计完整请求；请求体上限 4096 字节。
5. 完成 URL 解码、UTF-8 和参数格式校验；班级只映射到固定 data 目录。
6. 查询 `data/<class>.txt` 并生成 HTML 结果页；中文正常显示，输出内容须转义。
7. 正确设置 `Content-Type`、`Content-Length`、`Connection`，并使用 `send_all()` 发送。
8. 正确返回 400、403、404、405、413、415、500，并保留日志和统一清理。