1. # W3D2 HTTP 处理静态资源 Linux 下 HTTP Server 的设计

   电子科技大学

   ## 实训目标：让 V1.1 真正返回一个网站

   本次升级完成三件事:

   1. 把请求 URL 安全映射到 www 目录中的文件

   2. 根据扩展名返回正确的 Content-Type 和 Content-Length

   3. 分块发送文件，并保留事件管理､日志和 fd 清理闭环

      

      最终成果：浏览器能够完整加载 HTML､CSS､JavaScript､图片和 favicon｡

   ## W3D2 只新增静态文件层

   V1.1 已经具备:

   - epoll 管理多个 client_fd
   - HTTP 请求完整性判断､请求行和请求头解析
   - 200 / 404 响应､访问日志和系统日志

   V1.2 继续增加:

   - 请求路径￫ 本地文件￫ MIME 类型￫ 文件响应

   事件循环不重写，HTTP 公共解析和日志模块继续复用｡

   ## 一次静态资源请求的完整处理流程

   EPOLLIN 通知 client_fd 可读

   recv () 追加到该客户端独立缓冲区

   请求完整￫ 解析方法和目标路径

   规范化路径￫ 限定 www 根目录￫ stat ()

   确定状态码､MIME 和 Content-Length

   发送响应头￫ 分块发送文件￫ 日志￫ 删除事件￫ close ()

   ## URL 路径映射到固定网站根目录

   服务器启动时指定 document root, 例如 www/:

   GET/

   GET / ￫ www/index.html

   GET /index.html ￫ www/index.html

   GET /css/style.css ￫ www/css/style.css

   GET /js/app.js

   GET /js/app.js ￫ www/js/app.js

   GET /img/logo.jpeg ￫ www/img/logo.jpeg

   多级目录通过 URL 路径直接定位，不需要 opendir () 遍历整个网站目录｡

   ## 先规范化请求目标，再查找文件

   请求行示例：GET /css/style.css?v=1 HTTP/1.1

   1. 只处理路径部分，先去除？后面的查询参数
   2. 将 / 映射为 /index.html
   3. 在最终路径中提取扩展名，并统一转为小写
   4. GET 进入静态资源处理；其他方法返回 405 和 Allow: GET
   5. 不要直接把整条请求行或未经检查的 target 拼到本地路径

   输出结果:/css/style.css, 而不是 /css/style.css?v=1｡

   ## 网站根目录是静态文件的安全边界

   document root: 服务器允许公开的唯一目录，例如 /www/blog｡

   - 启动时使用 realpath () 得到根目录绝对路径
   - 拒绝空字节和包含.. 的路径段
   - 对存在的目标执行 realpath (), 结果必须仍以根目录 / 开头
   - 符号链接如果跳出根目录，返回 403
   - 不存在的文件返回 404, 不能回退读取服务器任意文件

   必须测试:curl --path-as-is [http://127.0.0.1:9001/../etc/passwd](https://link.wtturl.cn/?target=http%3A%2F%2F127.0.0.1%3A9001%2F..%2Fetc%2Fpasswd&scene=im&aid=497858&lang=zh)

   ## MIME 告诉浏览器如何解释响应体

   Content-Type 由资源类型决定，而不是由 HTTP 状态码决定｡

   相同的一串字节，MIME 不同，浏览器的处理方式也不同:

   - text/html: 解析成页面结构
   - text/css: 作为样式表加载
   - text/javascript: 作为脚本执行
   - image/png: 作为图片解码

   实现时先得到规范化文件路径，再根据最终扩展名查 MIME 映射表｡

   ## 常用静态资源 MIME 类型

   1. 文本文件:

   - text/plain: 纯文本文件

   - text/html:HTML 文档

   - text/css: CSS 样式表

   - text/javascript: JavaScript 脚本

     

     2. 图像文件:

   - image/jpeg: JPEG 图像

   - image/png: PNG 图像

   - image/gif: GIF 图像

   - image/svg+xml: SVG 图像

     

     3. 音频文件:

   - audio/mpeg: MP3 音频

   - audio/ogg: Ogg Vorbis 音频

     

     4. 视频文件:

   - video/mp4: MP4 视频

   - video/ogg: Ogg 视频

     

     5. 应用程序文件:

   - application/json: JSON 数据

   - application/xml: XML 数据

   - application/pdf: PDF 文档

   - application/zip:ZIP 压缩文件

     

     6. 二进制文件:

   - application/octet-stream: 用于传输二进制数据，如可执行文件或字节流

     

     7. 其他:

   - multipart/form-data: 用于表单提交，包含键值对和文件上传

   - application/x-www-form-urlencoded: 用于表单提交，键值对编码为 URL 参数

   ## 文本资源显式使用 UTF-8

   推荐映射:

   text/html; charset=utf-8 .html .htm .html.htm

   text/css; charset=utf-8 .css .CSS

   text/javascript; charset=utf-8 .js

   application/json; charset=utf-8 .json

   image/jpeg/image/png/image/gif/image/vnd.microsoft.icon

   未知扩展名使用 application/octet-stream｡

   现代 HTTP 实现不应依赖 “文本默认 US-ASCII”, 文本资源应显式写 charset｡

   媒体类型名称通常使用小写；二进制资源不要添加 charset｡

   ## stat () 同时提供文件类型和文件大小

   头文件:#include <sys/stat.h>

   c

   

   运行

   

   

   ```
   struct stat st;
   if (stat(file_path, &st) == -1) {
       /* 根据errno返回错误*/
   }
   ```

   st.st_mode: 判断目标是否为普通文件､目录或其他类型

   st.st_size: 文件字节数，直接用于响应 Content-Length

   stat () 失败时:

   - ENOENT / ENOTDIR ￫ 404
   - EACCES ￫ 403
   - 其他错误￫ 500 并写 system.log

   涉及符号链接安全时，结合 lstat () 或 realpath () 检查最终目标｡

   ## 静态服务器只发送允许公开的普通文件

   S_ISREG (st.st_mode) ￫ 可以发送，返回 200

   S_ISDIR (st.st_mode) ￫ 本次只允许 / 映射 index.html; 其他目录返回 403

   S_ISLNK (st.st_mode) ￫ 校验真实路径仍在 document root 内

   其他文件类型 ￫ 返回 403

   为什么不能直接发送所有路径？

   - 目录不是 HTTP 响应体文件
   - 设备､FIFO､socket 可能阻塞或泄露系统信息
   - 符号链接可能跳出网站根目录

   按请求路径直接 stat () 即可；本次无需 opendir () 和 readdir ()｡

   ## errno 只在系统调用明确失败后读取

   头文件:#include <errno.h>

   1. 先检查 open ()､stat ()､read () 等函数的返回值
   2. 只有返回值表示失败时，才读取 errno
   3. perror ("open") 把说明和 errno 对应信息写到标准错误 stderr
   4. strerror (errno) 返回错误字符串；安全输出:printf ("% s\n", strerror (errno));

   Web 服务器中的错误映射:

   ENOENT / ENOTDIR ￫ 404

   EACCES ￫ 403 →403

   其他 I/O 错误 ￫ 500

   成功调用后 errno 可能保留旧值，因此不能脱离失败返回值单独判断 errno｡

   ## 正确处理 open () 失败

   plaintext

   

   

   

   ```
   root@yan934:/home/dir1#./error.o
   No such file or directory root@yan934:/home/dir1# ./error.o:No such file or directory
   root@yan934:/home/dir1#./error.o
   No such file or directory root@yan934:/home/dir1# ./error.o:No such file or directory
   ```

   ## 响应头由文件元数据生成

   plaintext

   

   

   

   ```
   HTTP/1.1 200 OK\r\n
   Content-Type: text/css; charset=utf-8\r\n
   Content-Length: 1260\r\n
   Connection: close\r\n
   \r\n
   [1260字节文件内容]
   ```

   Content-Length 只计算响应体字节数，不包含状态行和响应头｡

   二进制文件必须按明确长度发送，不能使用 strlen () 判断文件长度｡

   发送完响应后统一删除事件并关闭 client_fd｡

   ## 固定大小缓冲区分块发送文件

   ### 错误做法

   c

   

   运行

   

   

   ```
   char *p = malloc(file_size);
   read(file_fd, p, file_size);
   send(client_fd, p, file_size, 0);
   free(p);
   ```

   文件越大，单个请求占用内存越多｡

   ### 推荐做法

   c

   

   运行

   

   

   ```
   char buffer[8192];
   while ((n = read(file_fd, buffer, sizeof(buffer))) > 0) {
       if (send_all(client_fd, buffer, (size_t)n) != 0) break;
   }
   close(file_fd);
   ```

   内存占用与文件大小解耦；不要把二进制文件当作 C 字符串处理

   一次 send () 不保证发送全部字节

   send_all () 必须维护已发送偏移:

   plaintext

   

   

   

   ```
   sent = 0
   while sent < length:
   n = send(fd, data + sent, length - sent, 0)
   n > 0 ￫ sent += n
   errno=EINTR ￫ 重试
   其他结果 ￫ 失败并进入统一清理
   ```

   基础版用于较小静态文件，可以采用阻塞发送｡

   进一步扩展非阻塞 fd 时，应保存待发送数据，并监听 EPOLLOUT 继续发送｡

   ## 文件处理仍然嵌入原有事件生命周期

   epoll_wait ()

   client_fd 可读：接收并累计 HTTP 请求

   请求完整:serve_static_file (client_fd, target)

   ￬

   路径校验￫ stat ￫ open ￫ 响应头￫ 文件内容

   ￬

   access.log 记录 URL､MIME､状态､字节数和耗时

   epoll_ctl (DEL) ￫ close (client_fd)

   ## 本次实训只需要六个核心状态码

   200 OK：普通文件成功发送

   400 Bad Request：路径或请求格式非法

   403 Forbidden：路径越界､无权限或不是普通文件

   404 Not Found：文件或中间目录不存在

   405 Method Not Allowed：不是 GET; 同时返回 Allow: GET

   500 Internal Server Error：服务器内部 I/O 错误

   状态码､响应正文､访问日志必须表达同一个结果｡

   304 缓存和 206 范围请求属于后续扩展，不纳入 V1.2 最低验收｡

   ## 404 表示请求的静态资源不存在

   404 Not Found

   ENOENT / ENOTDIR ￫ 404

   Not Found

   服务器上没有请求的资源

   The requested URL /a was not found on this server.

   响应仍要包含正确 Content-Type､Content-Length 和 Connection｡

   404 是一次访问结果，写入 access.log; 不能伪装成 200｡

   ## 403 表示服务器拒绝公开该目标

   重点测试:

   403｡ curl --path-as-is [http://127.0.0.1:9001/../etc/passwd](https://link.wtturl.cn/?target=http%3A%2F%2F127.0.0.1%3A9001%2F..%2Fetc%2Fpasswd&scene=im&aid=497858&lang=zh)

   预期：403, 且 www 之外的文件绝不能被读取｡

   不要在响应正文中泄露服务器真实绝对路径｡

   ## 日志和 fd 清理是响应的一部分

   access.log: 一次 HTTP 访问一条记录

   客户端 IP: 端口､方法､URL､MIME､状态码､响应字节数､耗时､发送结果

   system.log: 服务器运行和内部错误

   启动､退出､连接､断开､stat/open/read/send 失败､fd 和 errno

   统一清理顺序:

   close (file_fd) ￫ 删除 client_fd 事件￫ close (client_fd) ￫ 释放缓冲区

   404 首先是访问结果，不应当作为服务器内部 ERROR; 真正 I/O 异常才记录 ERROR｡

   ## 按可运行的小步骤完成 V1.2

   1. 建立 www / 目录并准备 index.html､CSS､JS､图片和 favicon
   2. 先完成 / 和 /index.html 映射，浏览器能看到 HTML
   3. 增加查询参数去除､路径规范化和 document root 校验
   4. 使用 stat () 判断文件并生成 Content-Length
   5. 根据扩展名返回 MIME, 使 CSS､JS 和图片正常加载
   6. 使用固定缓冲区和 send_all () 发送文件
   7. 补齐 403､404､405､500 和统一 fd 清理
   8. 最后增加访问日志､系统日志和自动测试证据

   每完成一步就运行 curl, 不要等全部代码写完后再调试｡

   ## 验收必须同时证明功能､安全和清理

   1) 功能证据

   - 浏览器首页完整显示；HTML､CSS､JS､图片和 favicon 均为 200
   - curl -i /､/css/style.css､/img/logo.jpeg 返回正确 Content-Type 和长度
   - 错误与安全证据

   - /missing 返回 404; 非 GET 返回 405;/../etc/passwd 被拒绝
   - 并发与清理证据

   - 3 个客户端连续访问互不影响
   - 响应后删除事件并关闭 client_fd; 日志包含 URL､状态码和字节数
   - 代码说明：学生能指出路径校验､MIME 映射､分块发送和统一清理的位置｡

   ## 实训内容：完成 mini_webserver V1.2

   每名学生独立在 W3D1 mini_webserver V1.1 基础上升级:

   - 建立 www / 博客目录，至少包含 HTML､CSS､JS､GIF/JPEG 和 ICO
   - GET / 与 GET /index.html 均返回首页，多级目录资源可正常加载
   - 正确设置 Content-Type､Content-Length 和 Connection
   - 固定缓冲区分块读取文件，并处理部分发送
   - 限定 document root, 拒绝目录穿越和非普通文件
   - 正确返回 200､400､403､404､405､500
   - 保留 epoll 事件模型､日志系统和统一 fd 清理

   最终提交：可运行源代码､网站资源､测试命令及日志 / 终端验收证据｡

   

   ## 学生本次实训前需提前准备

   可运行的 mini_webserver V1.1｡

   www / 目录及 index.html､CSS､JavaScript､JPEG 或 GIF 图片､ favicon.ico｡

   两个终端：一个运行服务器，一个执行 curl 测试｡

   可用于路径穿越测试的命令:

   - curl -i [http://127.0.0.1:9001/](https://link.wtturl.cn/?target=http%3A%2F%2F127.0.0.1%3A9001%2F&scene=im&aid=497858&lang=zh)
   - curl -i [http://127.0.0.1:9001/css/style.css](https://link.wtturl.cn/?target=http%3A%2F%2F127.0.0.1%3A9001%2Fcss%2Fstyle.css&scene=im&aid=497858&lang=zh)
   - curl -i [http://127.0.0.1:9001/missing](https://link.wtturl.cn/?target=http%3A%2F%2F127.0.0.1%3A9001%2Fmissing&scene=im&aid=497858&lang=zh)
   - curl -i -X POST [http://127.0.0.1:9001/](https://link.wtturl.cn/?target=http%3A%2F%2F127.0.0.1%3A9001%2F&scene=im&aid=497858&lang=zh)
   - curl -i --path-as-is [http://127.0.0.1:9001/../etc/passwd](https://link.wtturl.cn/?target=http%3A%2F%2F127.0.0.1%3A9001%2F..%2Fetc%2Fpasswd&scene=im&aid=497858&lang=zh)

   ## 运行测试建议

   ### 1. 正常资源

   bash

   

   运行

   

   

   ```
   curl -i http://127.0.0.1:9001/
   curl -i http://127.0.0.1:9001/index.html
   curl -i http://127.0.0.1:9001/css/style.css
   curl -s -D - -o /dev/null http://127.0.0.1:9001/img/logo.jpeg
   ```

   最后一条命令仍然发送 GET,-D - 把响应头输出到终端，-o /dev/null 丢弃图片响应体，避免二进制内容直接显示在终端中｡

   ### 2. 错误与安全

   bash

   

   运行

   

   

   ```
   curl -i http://127.0.0.1:9001/missing
   curl -i -X POST http://127.0.0.1:9001/
   curl -i --path-as-is http://127.0.0.1:9001/../etc/passwd
   ```

   ### 3. 三客户端访问

   在三个终端分别执行:

   bash

   

   运行

   

   

   ```
   curl -i http://127.0.0.1:9001/
   curl -i http://127.0.0.1:9001/css/style.css
   curl -i http://127.0.0.1:9001/img/logo.jpeg
   ```

   观察三个请求是否都能完成，服务器是否继续运行，日志是否出现三条对应访问记录｡

   ## 常见问题与提示

   表格

   

   

   

   |             现象             |        优先检查位置         |                       提示                       |
   | :--------------------------: | :-------------------------: | :----------------------------------------------: |
   |    首页能打开，CSS 无效果    |    CSS URL､路径映射､MIME    | 在浏览器 Network 中查看 CSS 状态码和 ContentType |
   |       图片只收到一部分       | read () 循环､send () 返回值 |        不能假设一次 send () 发送全部数据         |
   |       请求 / 返回 404        |    / 到 /index.html 映射    |       先打印规范化后的 URL 和最终文件路径        |
   |     所有资源长度都不正确     |     Content-Length 生成     |     文件长度来自 st.st_size, 不是 strlen ()      |
   | 请求不存在文件导致服务器退出 |     stat/open 错误分支      |    404 是正常访问结果，不能直接终止服务器进程    |
   |  运行一段时间后无法建立连接  |     fd 和客户端状态清理     |   检查所有分支是否删除事件､关闭 fd､释放缓冲区    |
   |   路径穿越能够读取外部文件   |     规范化和根目录校验      | realpath () 后必须验证目标仍在 document root 内  |
   |   非阻塞发送 CPU 占用很高    |         EAGAIN 处理         |       不要忙等，保存发送状态并等待可写事件       |

   ## 总结

   V1.1 解决的是 “服务器怎样收到并解析 HTTP 请求”,V1.2 解决的是 “服务器怎样把请求安全地转换为正确的静态文件响应”｡

   记住五个关键词：安全路径､文件元数据､正确 MIME､可靠发送､统一清理｡这五个部分任何一个缺失，服务器都可能出现资源加载失败､安全越界､文件截断或 fd 泄漏｡

   ## 思考

   1. 为什么路径映射必须限制在 document root 内？
   2. stat ()､lstat () 和 realpath () 在本项目中分别解决什么问题？
   3. 为什么 Content-Length 必须与实际发送的响应体字节数一致？
   4. 阻塞 fd 和非阻塞 fd 在处理部分发送时有什么差异？
   5. 为什么 404 通常写访问日志，而不是作为系统内部 ERROR?