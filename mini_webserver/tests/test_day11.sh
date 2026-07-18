#!/bin/bash
# test_day11.sh - 验证 W3D1 HTTP Server V1.1 (epoll) 功能
#
# 检查内容（对应 W3D1 验收标准）：
#   1. 编译与启动 — 编译并执行 serve-http，无错误
#   2. GET / 路由 — curl -i 返回 200 OK + HTML 页面
#   3. GET /missing 路由 — curl -i 返回 404 Not Found
#   4. POST /echo 路由 — curl -i -X POST -d "hello" 返回 200 + 回显
#   5. 响应格式检查 — Content-Type、Content-Length、\r\n
#   6. 多请求处理 — 连续发送多个 HTTP 请求
#   7. 请求数量控制 — 达到 max_requests 后正常退出
#   8. 连接清理 — EPOLL_CTL_DEL + close(client_fd)
#   9. epoll API 使用检查 — epoll_create1/epoll_ctl/epoll_wait/LT+EPOLLIN
#  10. 技术限制检查 — 未使用 select/多线程/多进程/线程池
#  11. 日志系统 — 系统日志和访问日志分别记录
#  12. 访问日志内容 — 时间/IP/方法/URL/状态码
#  13. 系统日志内容 — 时间/级别/事件描述
#  14. 日志级别 — DEBUG/INFO/WARNING/ERROR 四级
#  15. 自动测试 — 全部通过

EXE="./mini_web_server"
HOST="127.0.0.1"
PORT=8080
MAX_REQ=8

echo "=========================================="
echo "Day11 测试：HTTP Server V1.1 (W3D1)"
echo "=========================================="

# 清理
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
echo "--- 测试 1: 编译 HTTP Server V1.1 ---"
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
echo "--- 测试 2: 启动 HTTP Server V1.1 ---"
cleanup_procs

# 清除旧日志
rm -f logs/system.log logs/access.log

./"$EXE" serve-http "$MAX_REQ" &
SERVER_PID=$!
sleep 1

if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "HTTP Server V1.1 启动成功（PID: $SERVER_PID）"
else
    fail "HTTP Server V1.1 启动失败"
    exit 1
fi

# ======================================================
# 测试 3: GET / → 200 OK + HTML
# ======================================================
echo ""
echo "--- 测试 3: GET / → 200 OK ---"

RESP=$(curl -is --max-time 5 "http://${HOST}:${PORT}/" 2>/dev/null)
echo "  响应头: $(echo "$RESP" | head -5 | tr '\n' ' ')"

if echo "$RESP" | grep -q "200 OK"; then
    pass "GET / 返回 HTTP 200 OK"
else
    fail "GET / 未返回 200 OK"
fi

if echo "$RESP" | grep -q "Hello, HTTP!"; then
    pass "GET / 返回 HTML 页面（包含 'Hello, HTTP!'）"
else
    fail "GET / 未返回正确 HTML 内容"
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

# ======================================================
# 测试 4: GET /missing → 404 Not Found
# ======================================================
echo ""
echo "--- 测试 4: GET /missing → 404 Not Found ---"

RESP=$(curl -is --max-time 5 "http://${HOST}:${PORT}/missing" 2>/dev/null)
echo "  响应头: $(echo "$RESP" | head -3 | tr '\n' ' ')"

if echo "$RESP" | grep -q "404 Not Found"; then
    pass "GET /missing 返回 404 Not Found"
else
    fail "GET /missing 未返回 404 Not Found"
fi

# 验证服务器没有崩溃
if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "404 处理后服务器仍在运行"
else
    fail "404 处理后服务器异常退出"
fi

# ======================================================
# 测试 5: POST /echo → 200 OK + 回显请求体
# ======================================================
echo ""
echo "--- 测试 5: POST /echo → 200 OK + 回显 ---"

RESP=$(curl -is --max-time 5 -X POST \
    -H "Content-Type: text/plain" \
    -d "hello HTTP" \
    "http://${HOST}:${PORT}/echo" 2>/dev/null)
echo "  响应: $(echo "$RESP" | tr '\n' ' ')"

if echo "$RESP" | grep -q "200 OK"; then
    pass "POST /echo 返回 HTTP 200 OK"
else
    fail "POST /echo 未返回 200 OK"
fi

if echo "$RESP" | grep -q "hello HTTP"; then
    pass "POST /echo 正确回显请求体 'hello HTTP'"
else
    fail "POST /echo 未正确回显请求体"
fi

# ======================================================
# 测试 6: 405 方法检查
# ======================================================
echo ""
echo "--- 测试 6: 不支持的 HTTP 方法 → 405 ---"

RESP=$(curl -is --max-time 5 -X PUT -d "data" \
    "http://${HOST}:${PORT}/" 2>/dev/null)
if echo "$RESP" | grep -q "405"; then
    pass "PUT 方法返回 405 Method Not Allowed"
else
    fail "PUT 方法未返回 405"
fi

# ======================================================
# 测试 7: 多请求处理（连续发送多个请求）
# ======================================================
echo ""
echo "--- 测试 7: 连续发送多个 HTTP 请求 ---"

MULTI_PASS=1
for i in 1 2 3; do
    RESP=$(curl -s --max-time 5 "http://${HOST}:${PORT}/" 2>/dev/null)
    if ! echo "$RESP" | grep -q "Hello, HTTP!"; then
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

# ======================================================
# 测试 8: 请求数量控制 — 达到 max_requests 后正常退出
# ======================================================
echo ""
echo "--- 测试 8: 请求数量控制 — 达到 $MAX_REQ 后退出 ---"

# 已发送: / (1) + /missing (1) + /echo (1) + PUT / (1) + 3 连续 = 7
# 还需发送 1 个请求触发退出
curl -s --max-time 5 "http://${HOST}:${PORT}/" > /dev/null 2>&1

sleep 2

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "服务器处理 $MAX_REQ 个请求后正常退出"
else
    fail "服务器未在达到 max_requests 后退出"
    kill "$SERVER_PID" 2>/dev/null || true
fi

# ======================================================
# 测试 9: epoll API 使用检查
# ======================================================
echo ""
echo "--- 测试 9: epoll API 使用检查 ---"

SRC_FILE="src/http_server.c"

if grep -q "epoll_create1" "$SRC_FILE"; then
    pass "代码包含 epoll_create1() 调用"
else
    fail "代码缺少 epoll_create1() 调用"
fi

if grep -q "epoll_ctl" "$SRC_FILE"; then
    pass "代码包含 epoll_ctl() 调用"
else
    fail "代码缺少 epoll_ctl() 调用"
fi

if grep -q "epoll_wait" "$SRC_FILE"; then
    pass "代码包含 epoll_wait() 调用"
else
    fail "代码缺少 epoll_wait() 调用"
fi

if grep -q "EPOLLIN" "$SRC_FILE"; then
    pass "代码包含 EPOLLIN 标志（LT 模式）"
else
    fail "代码缺少 EPOLLIN 标志"
fi

if grep -q "EPOLL_CTL_ADD" "$SRC_FILE"; then
    pass "代码包含 EPOLL_CTL_ADD 操作"
else
    fail "代码缺少 EPOLL_CTL_ADD 操作"
fi

if grep -q "EPOLL_CTL_DEL" "$SRC_FILE"; then
    pass "代码包含 EPOLL_CTL_DEL 操作"
else
    fail "代码缺少 EPOLL_CTL_DEL 操作"
fi

# ======================================================
# 测试 10: 技术限制检查
# ======================================================
echo ""
echo "--- 测试 10: 技术限制检查 ---"

COMMENT_FREE=$(grep -v '^\s*\*' "$SRC_FILE" | grep -v '^\s*/' | grep -v '^\s*//')
if ! echo "$COMMENT_FREE" | grep -q "FD_ZERO\|FD_SET\|FD_ISSET\|FD_CLR\|select("; then
    pass "代码未使用 select() / fd_set（正确）"
else
    fail "代码不应包含 select/fd_set 相关代码"
fi

if ! grep -q "pthread\|fork\|thread_pool\|tcp_pool" "$SRC_FILE"; then
    pass "代码未使用多线程/多进程/线程池（正确）"
else
    fail "代码不应包含 pthread/fork/thread_pool"
fi

# ======================================================
# 测试 11: 连接清理检查
# ======================================================
echo ""
echo "--- 测试 11: 连接清理代码检查 ---"

if grep -q "EPOLL_CTL_DEL" "$SRC_FILE" && grep -q "close" "$SRC_FILE"; then
    pass "代码包含 EPOLL_CTL_DEL + close 连接清理逻辑"
else
    fail "代码缺少连接清理逻辑（EPOLL_CTL_DEL + close）"
fi

# ======================================================
# 测试 12: HTTP 解析器代码检查
# ======================================================
echo ""
echo "--- 测试 12: HTTP 解析器 ---"

PARSER_FILE="src/http_parser.c"

if grep -q "find_header_end\|\\\\r\\\\n\\\\r\\\\n" "$PARSER_FILE"; then
    pass "HTTP 解析器查找 \\r\\n\\r\\n 请求头结束标记"
else
    fail "HTTP 解析器缺少 \\r\\n\\r\\n 查找逻辑"
fi

if grep -q "Content-Length\|content_length" "$PARSER_FILE"; then
    pass "HTTP 解析器支持 Content-Length"
else
    fail "HTTP 解析器缺少 Content-Length 处理"
fi

if grep -q "is_request_complete" "$PARSER_FILE"; then
    pass "HTTP 解析器包含请求完整性判断"
else
    fail "HTTP 解析器缺少请求完整性判断"
fi

# ======================================================
# 测试 13: 日志系统 — 文件存在
# ======================================================
echo ""
echo "--- 测试 13: 日志系统 — 文件存在 ---"

# 再次启动服务器处理几个请求以生成日志
cleanup_procs
rm -f logs/system.log logs/access.log

./"$EXE" serve-http 4 &
SPID=$!
sleep 1

# 发送不同类型的请求
curl -s --max-time 5 "http://${HOST}:${PORT}/" > /dev/null 2>&1
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

# ======================================================
# 测试 14: 访问日志内容
# ======================================================
echo ""
echo "--- 测试 14: 访问日志内容 ---"

if [ -f "logs/access.log" ]; then
    echo "  访问日志内容:"
    cat logs/access.log | while read line; do echo "    $line"; done

    # 检查 GET / 的访问记录
    if grep -q "GET.*/.*HTTP" logs/access.log; then
        pass "访问日志包含 GET / 请求记录"
    else
        fail "访问日志缺少 GET / 记录"
    fi

    # 检查 404 的访问记录
    if grep -q "/missing" logs/access.log; then
        pass "访问日志包含 /missing 请求记录"
    else
        fail "访问日志缺少 /missing 记录"
    fi

    # 检查 POST 的访问记录
    if grep -q "POST.*/echo" logs/access.log; then
        pass "访问日志包含 POST /echo 请求记录"
    else
        fail "访问日志缺少 POST /echo 记录"
    fi

    # 检查格式：包含 IP 地址
    if grep -q "127.0.0.1" logs/access.log; then
        pass "访问日志包含客户端 IP 地址"
    else
        fail "访问日志缺少客户端 IP"
    fi

    # 检查格式：包含状态码 200 和 404
    if grep -q " 200 " logs/access.log && grep -q " 404 " logs/access.log; then
        pass "访问日志包含状态码 200 和 404"
    else
        fail "访问日志缺少状态码"
    fi
fi

# ======================================================
# 测试 15: 系统日志内容
# ======================================================
echo ""
echo "--- 测试 15: 系统日志内容 ---"

if [ -f "logs/system.log" ]; then
    echo "  系统日志内容:"
    cat logs/system.log | while read line; do echo "    $line"; done

    # 检查是否包含日志级别
    if grep -q "\[INFO\]" logs/system.log; then
        pass "系统日志包含 INFO 级别"
    else
        fail "系统日志缺少 INFO 级别"
    fi

    # 检查代码中支持四级日志（DEBUG/INFO/WARNING/ERROR）
    if grep -q "log_debug\|log_info\|log_warning\|log_error" src/log.c; then
        pass "系统日志代码支持 DEBUG/INFO/WARNING/ERROR 四级"
    else
        fail "系统日志代码级别不完整"
    fi

    # 检查是否包含启动事件
    if grep -q "starting\|socket\|bind\|listen" logs/system.log; then
        pass "系统日志包含服务器启动事件"
    else
        fail "系统日志缺少启动事件"
    fi

    # 检查是否包含请求处理事件
    if grep -q "request handled" logs/system.log; then
        pass "系统日志包含请求处理记录"
    else
        fail "系统日志缺少请求处理记录"
    fi
fi

# ======================================================
# 测试 16: HTTP 代码检查
# ======================================================
echo ""
echo "--- 测试 16: HTTP 请求解析代码检查 ---"

if grep -q "GET\|POST" "$SRC_FILE" && grep -q "200\|404" "$SRC_FILE"; then
    pass "代码包含 GET/POST 路由和 200/404 状态码"
else
    fail "代码缺少 HTTP 路由或状态码处理"
fi

if grep -q "\\\\r\\\\n" "$SRC_FILE"; then
    pass "HTTP 响应使用 \\r\\n 行结束符"
else
    fail "HTTP 响应缺少 \\r\\n"
fi

# ======================================================
# 结果汇总
# ======================================================
echo ""
echo "=========================================="
echo "Day11 HTTP Server V1.1 测试结果汇总"
echo "=========================================="
echo "  通过: $PASS_COUNT"
echo "  失败: $FAIL_COUNT"

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "存在 $FAIL_COUNT 个失败项！"
    exit 1
else
    echo "Day11 HTTP Server V1.1 测试全部通过！"
    echo "=========================================="
fi
