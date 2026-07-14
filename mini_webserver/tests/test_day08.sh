#!/bin/bash
# test_day08.sh - 验证 V0.8 线程池 TCP 网络服务器功能
#
# 检查内容（对应 W2D3 指导书）：
#   1. 并发请求：5 个 curl 同时发送 /hello，全部返回 200 OK
#   2. curl /hello        → HTTP/1.1 200 OK + "Hello, Web!"
#   3. curl /users/zhangsan → 用户信息（FOUND zhangsan）
#   4. curl /not-exist     → HTTP/1.1 404 Not Found
#   5. 日志中包含 socket/bind/listen/accept 记录
#   6. 日志中包含线程池特征（worker/thread/pthread/TID）
#   7. 日志包含 response status 记录
#   8. 服务器进程退出后无异常错误

# 不使用 set -e，改为手动检查每步的返回值

EXE="./mini_web_server"
CONF="conf/server.conf"
LOG="logs/server.log"
HOST="127.0.0.1"
PORT=8080

echo "=========================================="
echo "Day08 测试：线程池 TCP 网络服务器 (V0.8)"
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
    ./"$EXE" --pool "$CONF" 4 &
    SERVER_PID=$!
    sleep 1
}

# 停止服务器（带超时保护）
stop_server() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        # 等待最多 5 秒，超时则强制杀掉
        for i in 1 2 3 4 5; do
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
# 测试 6: 验证日志中的线程池特征
# ======================================================
echo ""
echo "--- 测试 6: 验证日志中的线程池记录 ---"

if grep -qi "V0.8" "$LOG" 2>/dev/null; then
    pass "日志包含 V0.8 标识"
else
    fail "日志无 V0.8 标识"
fi

if grep -qi "worker" "$LOG" 2>/dev/null; then
    pass "日志包含 worker 记录（线程池特征）"
else
    fail "日志无 worker 记录"
fi

if grep -qi "thread\|TID" "$LOG" 2>/dev/null; then
    pass "日志包含 thread/TID 信息"
else
    fail "日志无 thread/TID 信息"
fi

# ======================================================
# 测试 7: 验证日志中的响应状态记录
# ======================================================
echo ""
echo "--- 测试 7: 验证日志中的响应状态记录 ---"

if grep -qi "response status\|200 OK\|404 Not Found" "$LOG" 2>/dev/null; then
    pass "日志包含响应状态记录"
else
    fail "日志无响应状态记录"
fi

# ======================================================
# 测试 8: 验证日志无异常错误记录
# ======================================================
echo ""
echo "--- 测试 8: 验证日志无异常错误 ---"

# 服务器被 stop_server kill，不会写 "exiting normally"，
# 但应确保没有 error/fail/abort 等异常记录
if grep -qi "error\|fail\|abort\|crash" "$LOG" 2>/dev/null; then
    fail "日志包含异常错误记录"
else
    pass "日志无异常错误记录"
fi

# ======================================================
# 测试 9: 验证线程池关闭日志（仅服务器正常退出时有）
# ======================================================
#
# 注意：测试中使用 kill 停止服务器，因此 shutdown 日志不会出现
# （服务器在 accept 循环中被杀，未走正常关闭路径）。
# 这里只检查日志是否包含线程池创建记录和 worker 退出记录。
echo ""
echo "--- 测试 9: 验证线程池退出机制 ---"

# 检查 worker 是否处理了请求并正常完成了任务周期
if grep -qi "closed client fd=" "$LOG" 2>/dev/null; then
    pass "日志包含 worker 关闭客户端连接的记录"
else
    fail "日志无 worker 关闭客户端连接记录"
fi

# 如果服务器运行到 max_clients 并正常退出，会有 shutdown 日志
# 此处作为信息输出，不强制要求
if grep -qi "shutting down\|shutdown\|thread pool destroyed\|all workers exited" "$LOG" 2>/dev/null; then
    echo "  INFO: 日志包含线程池正常关闭记录（服务器正常退出）"
else
    echo "  INFO: 日志无线程池关闭记录（服务器被 kill 终止，符合预期）"
fi

# ======================================================
# 结果汇总
# ======================================================
echo ""
echo "=========================================="
echo "Day08 测试结果汇总"
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
    echo "Day08 测试全部通过！"
    echo "=========================================="
fi
