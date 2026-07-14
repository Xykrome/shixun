#!/bin/bash
# test_day06.sh - 验证 V0.6 TCP 网络服务器功能
#
# 检查内容（对应指导书 Slide 71）：
#   1. curl /hello        → HTTP/1.1 200 OK + "Hello, Web!"
#   2. curl /users/zhangsan → 用户信息（FOUND zhangsan）
#   3. curl /not-exist     → HTTP/1.1 404 Not Found
#   4. 服务器处理一个连接后正常退出
#   5. 日志中包含 socket/bind/listen/accept 记录
#
# 运行前确认：
#   - 需要安装 curl 命令（sudo apt install curl）
#   - 端口 8080 未被占用

set -e

EXE="./mini_web_server"
CONF="conf/server.conf"
LOG="logs/server.log"
HOST="127.0.0.1"
PORT=8080

echo "=========================================="
echo "Day06 测试：TCP 网络服务器 (V0.6)"
echo "=========================================="

# 编译
echo ""
echo "--- 编译 ---"
make clean > /dev/null 2>&1
make > /dev/null 2>&1
echo "编译完成"

# 清理日志和残留进程
rm -f "$LOG"

if ! command -v curl &> /dev/null; then
    echo "WARNING: curl 未安装，跳过 TCP 测试"
    echo "  安装命令: sudo apt install curl"
    exit 0
fi

# 确保端口未被占用
pkill -f "mini_web_server.*--tcp" 2>/dev/null || true
sleep 1

PASS_COUNT=0
FAIL_COUNT=0

pass() { echo "  PASS: $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { echo "  FAIL: $1"; FAIL_COUNT=$((FAIL_COUNT + 1)); }

# 启动服务器并等待就绪
start_server() {
    ./"$EXE" --tcp "$CONF" &
    SERVER_PID=$!
    sleep 1
}

# 停止服务器并等待端口释放
stop_server() {
    wait $SERVER_PID 2>/dev/null || true
    sleep 2  # 等待 TIME_WAIT 释放端口
}

# ======================================================
# 测试 1: curl /hello → HTTP/1.1 200 OK + Hello, Web!
# ======================================================
echo ""
echo "--- 测试 1: GET /hello → 200 OK ---"

start_server
# -i 输出 HTTP 头，-s 静默进度，--max-time 防卡死
RESP=$(curl -is --max-time 3 "http://${HOST}:${PORT}/hello" 2>&1 || true)
echo "  响应: $RESP"

if echo "$RESP" | grep -q "200 OK"; then
    pass "/hello 返回 HTTP 200 OK"
else
    fail "/hello 未返回 200 OK 状态"
fi

if echo "$RESP" | grep -q "Hello, Web!"; then
    pass "/hello 返回 Hello, Web!"
else
    fail "/hello 未返回预期内容"
fi

stop_server

# ======================================================
# 测试 2: curl /users/zhangsan → 用户信息
# ======================================================
echo ""
echo "--- 测试 2: GET /users/zhangsan → 用户信息 ---"

start_server
RESP=$(curl -is --max-time 3 "http://${HOST}:${PORT}/users/zhangsan" 2>&1 || true)
echo "  响应: $RESP"

if echo "$RESP" | grep -q "200 OK"; then
    pass "/users/zhangsan 返回 HTTP 200 OK"
else
    fail "/users/zhangsan 未返回 200 OK 状态"
fi

if echo "$RESP" | grep -q "FOUND zhangsan"; then
    pass "/users/zhangsan 返回 FOUND zhangsan"
else
    fail "/users/zhangsan 未返回用户信息"
fi

stop_server

# ======================================================
# 测试 3: curl /users/lisi → 另一个存在的用户
# ======================================================
echo ""
echo "--- 测试 3: GET /users/lisi → 用户信息 ---"

start_server
RESP=$(curl -is --max-time 3 "http://${HOST}:${PORT}/users/lisi" 2>&1 || true)
echo "  响应: $RESP"

if echo "$RESP" | grep -q "FOUND lisi"; then
    pass "/users/lisi 返回 FOUND lisi"
else
    fail "/users/lisi 未返回用户信息"
fi

stop_server

# ======================================================
# 测试 4: curl /not-exist → HTTP/1.1 404 Not Found
# ======================================================
echo ""
echo "--- 测试 4: GET /not-exist → 404 Not Found ---"

start_server
RESP=$(curl -is --max-time 3 "http://${HOST}:${PORT}/not-exist" 2>&1 || true)
echo "  响应: $RESP"

if echo "$RESP" | grep -q "404 Not Found"; then
    pass "/not-exist 返回 404 Not Found"
else
    fail "/not-exist 未返回 404"
fi

stop_server

# ======================================================
# 测试 5: 验证日志中包含 socket API 记录
# ======================================================
echo ""
echo "--- 测试 5: 验证日志中的 socket API 记录 ---"

if [ -f "$LOG" ]; then
    pass "日志文件已生成"
else
    fail "日志文件不存在"
fi

for api in socket bind listen accept recv send; do
    if grep -q "$api" "$LOG"; then
        pass "日志包含 $api() 记录"
    else
        fail "日志无 $api() 记录"
    fi
done

# ======================================================
# 测试 6: 验证日志包含正常退出记录
# ======================================================
echo ""
echo "--- 测试 6: 服务器正常退出 ---"

if grep -q "exiting normally" "$LOG"; then
    pass "日志包含正常退出记录"
else
    fail "日志无正常退出记录"
fi

# ======================================================
# 结果汇总
# ======================================================
echo ""
echo "=========================================="
echo "Day06 测试结果汇总"
echo "=========================================="
echo "  通过: $PASS_COUNT"
echo "  失败: $FAIL_COUNT"

echo ""
echo "--- 日志内容预览 ---"
cat "$LOG" 2>/dev/null || echo "(日志文件为空)"
echo ""

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "存在 $FAIL_COUNT 个失败项！"
    exit 1
else
    echo "Day06 测试全部通过！"
    echo "=========================================="
fi
