#!/bin/bash
# test_day10.sh - 验证 W2D5 Webserver V1.0 (epoll) 功能
#
# 检查内容（对应 W2D5 Webserver V1.0 验收标准）：
#   1. 编译与启动 — 编译并执行 serve-epoll，无错误
#   2. /hello 路由 — curl -i 返回 200 OK + 正确正文
#   3. 用户路由 — curl -i /users/zhangsan 返回 200 OK + 用户信息
#   4. 404 处理 — curl -i /missing 返回 404 Not Found
#   5. 多请求处理 — 连续发送多个 HTTP 请求
#   6. 请求数量控制 — 达到 max_requests 后正常退出
#   7. 连接清理 — EPOLL_CTL_DEL + close(client_fd)
#   8. epoll 代码检查 — epoll_create1/epoll_ctl/epoll_wait/LT+EPOLLIN
#   9. 技术限制检查 — 未使用 select/多线程/多进程/线程池
#  10. 自动测试 — 测试脚本全部通过

EXE="./mini_web_server"
HOST="127.0.0.1"
PORT=8080
MAX_REQ=8

echo "=========================================="
echo "Day10 测试：Epoll Webserver V1.0"
echo "=========================================="

# 清理残留进程
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
echo "--- 测试 1: 编译 Webserver V1.0 ---"
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
echo "--- 测试 2: 启动 Webserver V1.0 ---"
cleanup_procs

# 启动服务器（后台），处理 $MAX_REQ 个请求后自动退出
./"$EXE" serve-epoll "$MAX_REQ" &
SERVER_PID=$!
sleep 1

if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "Webserver V1.0 启动成功（PID: $SERVER_PID）"
else
    fail "Webserver V1.0 启动失败"
    exit 1
fi

# ======================================================
# 测试 3: /hello 路由
# ======================================================
echo ""
echo "--- 测试 3: GET /hello → 200 OK ---"

RESP=$(curl -is --max-time 5 "http://${HOST}:${PORT}/hello" 2>/dev/null)
echo "  响应: $(echo "$RESP" | tr '\n' ' ')"

if echo "$RESP" | grep -q "200 OK"; then
    pass "/hello 返回 HTTP 200 OK"
else
    fail "/hello 未返回 200 OK"
fi

if echo "$RESP" | grep -q "Hello, Web!"; then
    pass "/hello 返回 'Hello, Web!' 正文"
else
    fail "/hello 未返回正确正文"
fi

# ======================================================
# 测试 4: 用户路由
# ======================================================
echo ""
echo "--- 测试 4: GET /users/zhangsan → 200 OK ---"

RESP=$(curl -is --max-time 5 "http://${HOST}:${PORT}/users/zhangsan" 2>/dev/null)
echo "  响应: $(echo "$RESP" | tr '\n' ' ')"

if echo "$RESP" | grep -q "200 OK"; then
    pass "/users/zhangsan 返回 HTTP 200 OK"
else
    fail "/users/zhangsan 未返回 200 OK"
fi

if echo "$RESP" | grep -q "FOUND zhangsan\|张三"; then
    pass "/users/zhangsan 返回用户信息"
else
    fail "/users/zhangsan 未返回正确用户信息"
fi

# ======================================================
# 测试 5: 404 处理
# ======================================================
echo ""
echo "--- 测试 5: GET /missing → 404 Not Found ---"

RESP=$(curl -is --max-time 5 "http://${HOST}:${PORT}/missing" 2>/dev/null)
echo "  响应: $(echo "$RESP" | tr '\n' ' ')"

if echo "$RESP" | grep -q "404 Not Found"; then
    pass "/missing 返回 404 Not Found"
else
    fail "/missing 未返回 404 Not Found"
fi

# ======================================================
# 测试 6: 多请求处理（连续发送多个请求）
# ======================================================
echo ""
echo "--- 测试 6: 连续发送多个 HTTP 请求 ---"

MULTI_PASS=1
for i in 1 2 3; do
    RESP=$(curl -is --max-time 5 "http://${HOST}:${PORT}/hello" 2>/dev/null)
    if ! echo "$RESP" | grep -q "200 OK"; then
        MULTI_PASS=0
        echo "    请求 $i 失败"
    fi
done

# 还要验证服务器没崩溃
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
# 测试 7: 请求数量控制 — 达到 max_requests 后正常退出
# ======================================================
echo ""
echo "--- 测试 7: 请求数量控制 — 达到 $MAX_REQ 后退出 ---"

# 当前已发送: 1 (/hello) + 1 (/users) + 1 (/missing) + 3 (连续) = 6
# 还需发送 2 个请求触发退出
curl -s --max-time 5 "http://${HOST}:${PORT}/hello" > /dev/null 2>&1
curl -s --max-time 5 "http://${HOST}:${PORT}/hello" > /dev/null 2>&1

# 等待服务器处理完退出
sleep 2

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "服务器处理 $MAX_REQ 个请求后正常退出"
else
    fail "服务器未在达到 max_requests 后退出"
    kill "$SERVER_PID" 2>/dev/null || true
fi

# ======================================================
# 测试 8: epoll API 使用检查
# ======================================================
echo ""
echo "--- 测试 8: epoll API 使用检查 ---"

SRC_FILE="src/epoll_server.c"

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
# 测试 9: 技术限制检查
# ======================================================
echo ""
echo "--- 测试 9: 技术限制检查 ---"

# 排除注释行，检查代码中无 select/fd_set
# 注释行特征：以 * 或 / 或 // 开头（包括空格前缀）
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
# 测试 10: 连接清理检查
# ======================================================
echo ""
echo "--- 测试 10: 连接清理代码检查 ---"

if grep -q "EPOLL_CTL_DEL" "$SRC_FILE" && grep -q "close" "$SRC_FILE"; then
    pass "代码包含 EPOLL_CTL_DEL + close 连接清理逻辑"
else
    fail "代码缺少连接清理逻辑（EPOLL_CTL_DEL + close）"
fi

# ======================================================
# 结果汇总
# ======================================================
echo ""
echo "=========================================="
echo "Day10 Webserver V1.0 测试结果汇总"
echo "=========================================="
echo "  通过: $PASS_COUNT"
echo "  失败: $FAIL_COUNT"

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "存在 $FAIL_COUNT 个失败项！"
    exit 1
else
    echo "Day10 Webserver V1.0 测试全部通过！"
    echo "=========================================="
fi
