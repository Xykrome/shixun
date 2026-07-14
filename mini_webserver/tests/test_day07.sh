#!/bin/bash
# test_day07.sh - 验证 V0.7 多进程 TCP 网络服务器功能
#
# 检查内容（对应 W2D2 指导书 Slide 31-37）：
#   1. 并发请求：5 个 curl 同时发送 /hello，全部返回 200 OK
#   2. curl /hello        → HTTP/1.1 200 OK + "Hello, Web!"
#   3. curl /users/zhangsan → 用户信息（FOUND zhangsan）
#   4. curl /not-exist     → HTTP/1.1 404 Not Found
#   5. 日志中包含 fork/child/PID 记录（多进程特征）
#   6. 日志中包含 socket/bind/listen/accept 记录
#   7. 服务器进程退出后无僵尸进程残留

# 不使用 set -e，改为手动检查每步的返回值

EXE="./mini_web_server"
CONF="conf/server.conf"
LOG="logs/server.log"
HOST="127.0.0.1"
PORT=8080

echo "=========================================="
echo "Day07 测试：多进程 TCP 网络服务器 (V0.7)"
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
    echo "WARNING: curl 未安装，跳过测试"
    echo "  安装命令: sudo apt install curl"
    exit 0
fi

# 确保端口未被占用
pkill -f "mini_web_server" 2>/dev/null || true
sleep 1

PASS_COUNT=0
FAIL_COUNT=0

pass() { echo "  PASS: $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { echo "  FAIL: $1"; FAIL_COUNT=$((FAIL_COUNT + 1)); }

# 启动服务器
start_server() {
    ./"$EXE" --fork "$CONF" &
    SERVER_PID=$!
    sleep 1
}

# 停止服务器（带超时保护）
stop_server() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        # 等待最多 3 秒，超时则强制杀掉
        for i in 1 2 3; do
            if ! kill -0 "$SERVER_PID" 2>/dev/null; then
                break
            fi
            sleep 1
        done
        # 如果还没退出，强制杀死
        kill -9 "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    sleep 1
}

# 单个 curl 请求（带超时）
do_curl() {
    curl -is --max-time 5 "$@" 2>/dev/null
}

# ======================================================
# 测试 1: 并发 5 个 curl 到 /hello
# ======================================================
echo ""
echo "--- 测试 1: 并发 5 个 GET /hello → 全部 200 OK ---"

start_server

# 用临时文件收集结果，避免 set -e + wait 的问题
TMPDIR=$(mktemp -d)
CONCURRENT=5
PIDS=""

for i in $(seq 1 $CONCURRENT); do
    (do_curl "http://${HOST}:${PORT}/hello" > "${TMPDIR}/resp_${i}" 2>/dev/null) &
    PIDS="$PIDS $!"
done

# 等待所有后台 curl 完成（最多 8 秒）
for pid in $PIDS; do
    wait $pid 2>/dev/null || true
done

# 检查结果
ALL_PASS=1
for i in $(seq 1 $CONCURRENT); do
    if [ -f "${TMPDIR}/resp_${i}" ] && [ -s "${TMPDIR}/resp_${i}" ]; then
        if grep -q "Hello, Web!" "${TMPDIR}/resp_${i}"; then
            :
        else
            echo "    请求 $i 响应不正确: $(tr '\n' ' ' < "${TMPDIR}/resp_${i}")"
            ALL_PASS=0
        fi
    else
        echo "    请求 $i 无响应或为空"
        ALL_PASS=0
    fi
done

if [ "$ALL_PASS" -eq 1 ]; then
    pass "并发 $CONCURRENT 个 /hello 请求全部返回 Hello, Web!"
else
    fail "并发 /hello 请求存在失败"
fi

rm -rf "$TMPDIR"
stop_server

# ======================================================
# 测试 2: curl /hello → 200 OK + Hello, Web!
# ======================================================
echo ""
echo "--- 测试 2: GET /hello → 200 OK ---"

start_server
RESP=$(do_curl "http://${HOST}:${PORT}/hello")
echo "  响应: $(echo "$RESP" | tr '\n' ' ')"

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
# 测试 3: curl /users/zhangsan → 用户信息
# ======================================================
echo ""
echo "--- 测试 3: GET /users/zhangsan → 用户信息 ---"

start_server
RESP=$(do_curl "http://${HOST}:${PORT}/users/zhangsan")
echo "  响应: $(echo "$RESP" | tr '\n' ' ')"

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
# 测试 4: curl /not-exist → 404 Not Found
# ======================================================
echo ""
echo "--- 测试 4: GET /not-exist → 404 Not Found ---"

start_server
RESP=$(do_curl "http://${HOST}:${PORT}/not-exist")
echo "  响应: $(echo "$RESP" | tr '\n' ' ')"

if echo "$RESP" | grep -q "404 Not Found"; then
    pass "/not-exist 返回 404 Not Found"
else
    fail "/not-exist 未返回 404"
fi

stop_server

# ======================================================
# 测试 5: 验证日志中的 socket API 记录
# ======================================================
echo ""
echo "--- 测试 5: 验证日志中的 socket API 记录 ---"

if [ -f "$LOG" ]; then
    pass "日志文件已生成"
else
    fail "日志文件不存在"
fi

for api in socket bind listen accept recv send; do
    if grep -q "$api" "$LOG" 2>/dev/null; then
        pass "日志包含 $api() 记录"
    else
        fail "日志无 $api() 记录"
    fi
done

# ======================================================
# 测试 6: 验证日志中的多进程特征（fork/child/PID）
# ======================================================
echo ""
echo "--- 测试 6: 验证日志中的多进程记录 ---"

if grep -qi "child\|fork" "$LOG" 2>/dev/null; then
    pass "日志包含 child/fork 记录（多进程特征）"
else
    fail "日志无 child/fork 记录"
fi

if grep -qi "PID" "$LOG" 2>/dev/null; then
    pass "日志包含 PID 信息"
else
    fail "日志无 PID 信息"
fi

# ======================================================
# 测试 7: 验证日志无异常错误记录
# ======================================================
echo ""
echo "--- 测试 7: 验证日志无异常错误 ---"

# 服务器被 stop_server kill，不会写 "exiting normally"，
# 但应确保没有 error/fail/abort 等异常记录
if grep -qi "error\|fail\|abort\|crash" "$LOG" 2>/dev/null; then
    fail "日志包含异常错误记录"
else
    pass "日志无异常错误记录"
fi

# ======================================================
# 测试 8: 验证无僵尸进程
# ======================================================
echo ""
echo "--- 测试 8: 验证无僵尸进程 ---"

# 检查是否有僵尸进程（状态为 Z）
ZOMBIE_Z=$(ps -eo stat 2>/dev/null | grep -c "^Z" || true)
ZOMBIE_Z=$(printf '%d' "${ZOMBIE_Z:-0}" 2>/dev/null || echo 0)
if [ "${ZOMBIE_Z:-0}" -eq 0 ] 2>/dev/null; then
    pass "无僵尸进程残留（系统共 $ZOMBIE_Z 个）"
else
    fail "存在僵尸进程（系统共 $ZOMBIE_Z 个）"
fi

# ======================================================
# 结果汇总
# ======================================================
echo ""
echo "=========================================="
echo "Day07 测试结果汇总"
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
    echo "Day07 测试全部通过！"
    echo "=========================================="
fi
