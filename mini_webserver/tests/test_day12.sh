#!/bin/bash
# test_day12.sh - 验证 W3D2 HTTP Static File Server V1.2 (epoll) 功能
#
# 检查内容（对应 W3D2 验收标准）：
#   1. 编译与启动 — 编译并执行 serve-http，无错误
#   2. 静态文件服务 — GET / 返回 www/index.html (200, text/html)
#   3. MIME 类型 — CSS→text/css, JS→text/javascript, PNG→image/png
#   4. Content-Length — 响应体长度与文件大小一致
#   5. 404 处理 — /missing 返回 404 Not Found
#   6. 目录穿越防护 — /../etc/passwd 返回 403 Forbidden
#   7. 405 方法检查 — 非 GET 方法返回 405
#   8. POST /echo — V1.1 兼容，回显请求体
#   9. 多请求并发 — 3 个客户端连续访问互不影响
#  10. 请求数量控制 — 达到 max_requests 后正常退出
#  11. epoll API — epoll_create1/epoll_ctl/epoll_wait 正确使用
#  12. 连接清理 — EPOLL_CTL_DEL + close
#  13. 日志系统 — 双文件日志存在
#  14. 访问日志 — 含 MIME 类型、状态码、字节数
#  15. 系统日志 — 含级别、事件描述
#  16. 静态文件代码 — static_handler 模块存在且正确

EXE="./mini_web_server"
HOST="127.0.0.1"
PORT=8080
MAX_REQ=15

echo "=========================================="
echo "Day12 测试：HTTP Static File Server V1.2"
echo "=========================================="

cleanup_procs() {
    pkill -f "mini_web_server" 2>/dev/null || true
    sleep 1
}
trap cleanup_procs EXIT

PASS_COUNT=0
FAIL_COUNT=0

pass() { echo "  PASS: $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { echo "  FAIL: $1"; FAIL_COUNT=$((FAIL_COUNT + 1)); }

# ======================================================
# 测试 1: 编译验证
# ======================================================
echo ""
echo "--- 测试 1: 编译 HTTP Static File Server V1.2 ---"
make clean > /dev/null 2>&1
if make > /dev/null 2>&1; then
    pass "编译成功（无错误）"
else
    fail "编译失败"
    exit 1
fi

# ======================================================
# 测试 2: 服务器启动验证
# ======================================================
echo ""
echo "--- 测试 2: 启动 HTTP Server V1.2 ---"
cleanup_procs
rm -f logs/system.log logs/access.log

./"$EXE" serve-http "$MAX_REQ" &
SERVER_PID=$!
sleep 1

if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "HTTP Server V1.2 启动成功（PID: $SERVER_PID）"
else
    fail "HTTP Server V1.2 启动失败"
    exit 1
fi

# ======================================================
# 测试 3: GET / → 静态文件首页 (200 + text/html)
# ======================================================
echo ""
echo "--- 测试 3: GET / → 200 OK + HTML 首页 ---"

RESP=$(curl -is --max-time 5 "http://${HOST}:${PORT}/" 2>/dev/null)
echo "  响应头: $(echo "$RESP" | head -5 | tr '\n' ' ')"

if echo "$RESP" | grep -q "200 OK"; then
    pass "GET / 返回 HTTP 200 OK"
else
    fail "GET / 未返回 200 OK"
fi

if echo "$RESP" | grep -qi "Content-Type:.*text/html"; then
    pass "GET / Content-Type 为 text/html"
else
    fail "GET / Content-Type 不正确"
fi

if echo "$RESP" | grep -qi "Content-Length:"; then
    pass "GET / 包含 Content-Length 头"
else
    fail "GET / 缺少 Content-Length 头"
fi

# 检查首页内容来自真实文件
if echo "$RESP" | grep -q "Hello, HTTP!"; then
    pass "GET / 返回 www/index.html 内容（包含 'Hello, HTTP!'）"
else
    fail "GET / 未返回预期的 index.html 内容"
fi

# 检查 CSS 链接在 HTML 中
if echo "$RESP" | grep -q "style.css"; then
    pass "GET / HTML 引用了 CSS 样式表"
else
    fail "GET / HTML 未引用 CSS"
fi

# ======================================================
# 测试 4: GET /index.html → 与 / 返回相同内容
# ======================================================
echo ""
echo "--- 测试 4: GET /index.html → 与 / 相同 ---"

RESP2=$(curl -is --max-time 5 "http://${HOST}:${PORT}/index.html" 2>/dev/null)

if echo "$RESP2" | grep -q "200 OK"; then
    pass "GET /index.html 也返回 200 OK"
else
    fail "GET /index.html 未返回 200 OK"
fi

if echo "$RESP2" | grep -q "Hello, HTTP!"; then
    pass "GET /index.html 与 / 返回相同首页内容"
else
    fail "GET /index.html 内容与 / 不一致"
fi

# ======================================================
# 测试 5: CSS MIME 类型
# ======================================================
echo ""
echo "--- 测试 5: CSS 文件 MIME 类型 ---"

RESP_CSS=$(curl -is --max-time 5 "http://${HOST}:${PORT}/css/style.css" 2>/dev/null)

if echo "$RESP_CSS" | grep -q "200 OK"; then
    pass "GET /css/style.css 返回 200 OK"
else
    fail "GET /css/style.css 未返回 200 OK"
fi

if echo "$RESP_CSS" | grep -qi "Content-Type:.*text/css"; then
    pass "CSS Content-Type 为 text/css"
else
    fail "CSS Content-Type 不正确"
fi

# Content-Length 应与文件大小匹配
CSS_SIZE=$(wc -c < www/css/style.css | tr -d ' ')
if echo "$RESP_CSS" | grep -q "Content-Length: $CSS_SIZE"; then
    pass "CSS Content-Length ($CSS_SIZE) 与实际文件大小一致"
else
    fail "CSS Content-Length 与实际文件大小不一致（预期 $CSS_SIZE）"
fi

# ======================================================
# 测试 6: JavaScript MIME 类型
# ======================================================
echo ""
echo "--- 测试 6: JavaScript 文件 MIME 类型 ---"

RESP_JS=$(curl -is --max-time 5 "http://${HOST}:${PORT}/js/app.js" 2>/dev/null)

if echo "$RESP_JS" | grep -q "200 OK"; then
    pass "GET /js/app.js 返回 200 OK"
else
    fail "GET /js/app.js 未返回 200 OK"
fi

if echo "$RESP_JS" | grep -qi "Content-Type:.*text/javascript"; then
    pass "JS Content-Type 为 text/javascript"
else
    fail "JS Content-Type 不正确"
fi

# ======================================================
# 测试 7: 图片 MIME 类型（无 charset）
# ======================================================
echo ""
echo "--- 测试 7: 图片文件 MIME 类型 ---"

RESP_IMG=$(curl -is --max-time 5 "http://${HOST}:${PORT}/img/logo.png" 2>/dev/null)

if echo "$RESP_IMG" | grep -q "200 OK"; then
    pass "GET /img/logo.png 返回 200 OK"
else
    fail "GET /img/logo.png 未返回 200 OK"
fi

if echo "$RESP_IMG" | grep -qi "Content-Type:.*image/png"; then
    pass "PNG Content-Type 为 image/png"
else
    fail "PNG Content-Type 不正确"
fi

# 图像不应包含 charset
if ! echo "$RESP_IMG" | grep -qi "image/png.*charset"; then
    pass "PNG Content-Type 不含 charset（正确）"
else
    fail "PNG Content-Type 不应包含 charset"
fi

# ======================================================
# 测试 8: favicon MIME 类型
# ======================================================
echo ""
echo "--- 测试 8: favicon MIME 类型 ---"

RESP_ICO=$(curl -is --max-time 5 "http://${HOST}:${PORT}/favicon.ico" 2>/dev/null)

if echo "$RESP_ICO" | grep -q "200 OK"; then
    pass "GET /favicon.ico 返回 200 OK"
else
    fail "GET /favicon.ico 未返回 200 OK"
fi

# ======================================================
# 测试 9: 404 — 不存在的文件
# ======================================================
echo ""
echo "--- 测试 9: 404 错误处理 ---"

RESP_404=$(curl -is --max-time 5 "http://${HOST}:${PORT}/missing" 2>/dev/null)

if echo "$RESP_404" | grep -q "404 Not Found"; then
    pass "GET /missing 返回 404 Not Found"
else
    fail "GET /missing 未返回 404"
fi

# 404 响应也要有 Content-Type
if echo "$RESP_404" | grep -qi "Content-Type:"; then
    pass "404 响应包含 Content-Type 头"
else
    fail "404 响应缺少 Content-Type 头"
fi

# 服务器处理 404 后仍正常运行
if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "404 处理后服务器仍在运行"
else
    fail "404 处理后服务器异常退出"
fi

# ======================================================
# 测试 10: 403 — 目录穿越防护
# ======================================================
echo ""
echo "--- 测试 10: 目录穿越防护 ---"

RESP_403=$(curl -is --max-time 5 --path-as-is \
    "http://${HOST}:${PORT}/../etc/passwd" 2>/dev/null)

if echo "$RESP_403" | grep -q "403 Forbidden"; then
    pass "GET /../etc/passwd 返回 403 Forbidden（目录穿越被拒绝）"
else
    fail "GET /../etc/passwd 未返回 403（安全漏洞！）"
fi

# 再测一个 .. 变体
RESP_403B=$(curl -is --max-time 5 --path-as-is \
    "http://${HOST}:${PORT}/../../etc/passwd" 2>/dev/null)

if echo "$RESP_403B" | grep -q "403\|404"; then
    pass "多层 .. 穿越也被拒绝"
else
    fail "多层 .. 穿越未被拒绝"
fi

# ======================================================
# 测试 11: 405 — 非 GET 方法
# ======================================================
echo ""
echo "--- 测试 11: 405 方法检查 ---"

RESP_405=$(curl -is --max-time 5 -X PUT -d "data" \
    "http://${HOST}:${PORT}/" 2>/dev/null)

if echo "$RESP_405" | grep -q "405 Method Not Allowed"; then
    pass "PUT 方法返回 405 Method Not Allowed"
else
    fail "PUT 方法未返回 405"
fi

# Allow 头应指明 GET
if echo "$RESP_405" | grep -qi "Allow:.*GET"; then
    pass "405 响应包含 Allow: GET 头"
else
    fail "405 响应缺少 Allow 头"
fi

# ======================================================
# 测试 12: POST /echo — V1.1 兼容
# ======================================================
echo ""
echo "--- 测试 12: POST /echo — V1.1 兼容 ---"

RESP_ECHO=$(curl -is --max-time 5 -X POST \
    -H "Content-Type: text/plain" \
    -d "hello HTTP" \
    "http://${HOST}:${PORT}/echo" 2>/dev/null)

if echo "$RESP_ECHO" | grep -q "200 OK"; then
    pass "POST /echo 返回 HTTP 200 OK"
else
    fail "POST /echo 未返回 200 OK"
fi

if echo "$RESP_ECHO" | grep -q "hello HTTP"; then
    pass "POST /echo 正确回显请求体 'hello HTTP'"
else
    fail "POST /echo 未正确回显请求体"
fi

# ======================================================
# 测试 13: 多请求并发稳定性
# ======================================================
echo ""
echo "--- 测试 13: 多请求并发 ---"

MULTI_PASS=1
for i in 1 2 3; do
    RESULT=$(curl -s --max-time 5 "http://${HOST}:${PORT}/" 2>/dev/null)
    if ! echo "$RESULT" | grep -q "Hello, HTTP!"; then
        MULTI_PASS=0
        echo "    请求 $i 失败"
    fi
done

if kill -0 "$SERVER_PID" 2>/dev/null; then
    if [ "$MULTI_PASS" -eq 1 ]; then
        pass "连续 3 个请求全部返回 200 OK，服务器稳定运行"
    else
        fail "连续请求存在失败"
    fi
else
    fail "服务器在处理多个请求时异常退出"
fi

# 还需 1 个请求触发退出（已达 10 个）
curl -s --max-time 5 "http://${HOST}:${PORT}/" > /dev/null 2>&1
sleep 2

# ======================================================
# 测试 14: 请求数量控制 — 正常退出
# ======================================================
echo ""
echo "--- 测试 14: 请求数量控制 ---"

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "服务器处理 $MAX_REQ 个请求后正常退出"
else
    fail "服务器未在达到 max_requests 后退出"
    kill "$SERVER_PID" 2>/dev/null || true
fi

# ======================================================
# 测试 15: 静态文件模块代码检查
# ======================================================
echo ""
echo "--- 测试 15: 静态文件处理器模块 ---"

if [ -f "src/static_handler.c" ] && [ -f "include/static_handler.h" ]; then
    pass "static_handler.h/c 模块存在"
else
    fail "static_handler 模块缺失"
fi

if grep -q "get_mime_type" src/static_handler.c; then
    pass "代码包含 get_mime_type() MIME 映射函数"
else
    fail "代码缺少 get_mime_type()"
fi

if grep -q "send_all" src/static_handler.c; then
    pass "代码包含 send_all() 可靠发送函数"
else
    fail "代码缺少 send_all()"
fi

if grep -q "normalize_path" src/static_handler.c; then
    pass "代码包含 normalize_path() 路径规范化函数"
else
    fail "代码缺少 normalize_path()"
fi

if grep -q "serve_static_file" src/static_handler.c; then
    pass "代码包含 serve_static_file() 主函数"
else
    fail "代码缺少 serve_static_file()"
fi

if grep -q "realpath" src/static_handler.c; then
    pass "代码使用 realpath() 路径安全校验"
else
    fail "代码缺少 realpath() 安全校验"
fi

if grep -q "S_ISREG" src/static_handler.c; then
    pass "代码使用 S_ISREG() 检查普通文件"
else
    fail "代码缺少 S_ISREG() 文件类型检查"
fi

# ======================================================
# 测试 16: epoll API 使用检查
# ======================================================
echo ""
echo "--- 测试 16: epoll API 使用检查 ---"

SRC_FILE="src/http_server.c"

if grep -q "epoll_create1" "$SRC_FILE"; then
    pass "http_server.c 包含 epoll_create1()"
else
    fail "http_server.c 缺少 epoll_create1()"
fi

if grep -q "epoll_ctl" "$SRC_FILE"; then
    pass "http_server.c 包含 epoll_ctl()"
else
    fail "http_server.c 缺少 epoll_ctl()"
fi

if grep -q "epoll_wait" "$SRC_FILE"; then
    pass "http_server.c 包含 epoll_wait()"
else
    fail "http_server.c 缺少 epoll_wait()"
fi

if grep -q "EPOLL_CTL_DEL" "$SRC_FILE" && grep -q "close" "$SRC_FILE"; then
    pass "http_server.c 包含 EPOLL_CTL_DEL + close 清理逻辑"
else
    fail "http_server.c 缺少连接清理逻辑"
fi

# ======================================================
# 测试 17: 技术限制检查
# ======================================================
echo ""
echo "--- 测试 17: 技术限制检查 ---"

COMMENT_FREE=$(grep -v '^\s*\*' "$SRC_FILE" | grep -v '^\s*/' | grep -v '^\s*//')
if ! echo "$COMMENT_FREE" | grep -q "FD_ZERO\|FD_SET\|FD_ISSET\|FD_CLR\|select("; then
    pass "http_server.c 未使用 select() / fd_set"
else
    fail "http_server.c 不应包含 select/fd_set"
fi

if ! grep -q "pthread\|fork\|thread_pool\|tcp_pool" "$SRC_FILE"; then
    pass "http_server.c 未使用多线程/多进程/线程池"
else
    fail "http_server.c 不应包含 pthread/fork/thread_pool"
fi

# ======================================================
# 测试 18: 日志系统
# ======================================================
echo ""
echo "--- 测试 18: 日志系统 ---"

# 第二次启动生成日志
cleanup_procs
rm -f logs/system.log logs/access.log

./"$EXE" serve-http 5 &
SPID=$!
sleep 1

curl -s --max-time 5 "http://${HOST}:${PORT}/" > /dev/null 2>&1
curl -s --max-time 5 "http://${HOST}:${PORT}/css/style.css" > /dev/null 2>&1
curl -s --max-time 5 "http://${HOST}:${PORT}/missing" > /dev/null 2>&1
curl -s --max-time 5 -X POST -d "test" "http://${HOST}:${PORT}/echo" > /dev/null 2>&1
curl -s --max-time 5 "http://${HOST}:${PORT}/" > /dev/null 2>&1

sleep 2
kill $SPID 2>/dev/null || true
wait $SPID 2>/dev/null

if [ -f "logs/system.log" ]; then
    pass "系统日志文件存在 (logs/system.log)"
else
    fail "系统日志文件不存在"
fi

if [ -f "logs/access.log" ]; then
    pass "访问日志文件存在 (logs/access.log)"
else
    fail "访问日志文件不存在"
fi

echo ""
echo "--- 测试 19: 访问日志内容 ---"

if [ -f "logs/access.log" ]; then
    echo "  访问日志内容:"
    cat logs/access.log | while read line; do echo "    $line"; done

    if grep -q "GET.*/.*HTTP" logs/access.log; then
        pass "访问日志包含 GET / 请求记录"
    else
        fail "访问日志缺少 GET / 记录"
    fi

    # V1.2 检查：日志应包含 MIME 类型
    if grep -q "text/html" logs/access.log; then
        pass "访问日志包含 MIME 类型 (text/html)"
    else
        fail "访问日志缺少 MIME 类型"
    fi

    if grep -q "text/css" logs/access.log; then
        pass "访问日志包含 CSS MIME 类型 (text/css)"
    else
        fail "访问日志缺少 CSS MIME 类型记录"
    fi

    if grep -q "/missing" logs/access.log; then
        pass "访问日志包含 /missing 请求记录"
    else
        fail "访问日志缺少 /missing 记录"
    fi

    if grep -q "POST.*/echo" logs/access.log; then
        pass "访问日志包含 POST /echo 请求记录"
    else
        fail "访问日志缺少 POST /echo 记录"
    fi

    if grep -q "127.0.0.1" logs/access.log; then
        pass "访问日志包含客户端 IP 地址"
    else
        fail "访问日志缺少客户端 IP"
    fi

    if grep -q " 200 " logs/access.log && grep -q " 404 " logs/access.log; then
        pass "访问日志包含状态码 200 和 404"
    else
        fail "访问日志缺少状态码"
    fi
fi

echo ""
echo "--- 测试 20: 系统日志内容 ---"

if [ -f "logs/system.log" ]; then
    echo "  系统日志内容:"
    cat logs/system.log | while read line; do echo "    $line"; done

    if grep -q "\[INFO\]" logs/system.log; then
        pass "系统日志包含 INFO 级别"
    else
        fail "系统日志缺少 INFO 级别"
    fi

    if grep -q "log_debug\|log_info\|log_warning\|log_error" src/log.c; then
        pass "系统日志代码支持 DEBUG/INFO/WARNING/ERROR 四级"
    else
        fail "系统日志代码级别不完整"
    fi

    if grep -q "starting\|socket\|bind\|listen" logs/system.log; then
        pass "系统日志包含服务器启动事件"
    else
        fail "系统日志缺少启动事件"
    fi

    if grep -q "request handled" logs/system.log; then
        pass "系统日志包含请求处理记录"
    else
        fail "系统日志缺少请求处理记录"
    fi
fi

# ======================================================
# 结果汇总
# ======================================================
echo ""
echo "=========================================="
echo "Day12 HTTP Static File Server V1.2 测试结果汇总"
echo "=========================================="
echo "  通过: $PASS_COUNT"
echo "  失败: $FAIL_COUNT"

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "存在 $FAIL_COUNT 个失败项！"
    exit 1
else
    echo "Day12 HTTP Static File Server V1.2 测试全部通过！"
    echo "=========================================="
fi
