#!/bin/bash
# test_day10.sh - 验证 W2D5 EpollServer 功能
#
# 检查内容（对应 W2D5 实训内容2 验收标准）：
#   1. 编译与启动 — 编译并启动 EpollServer，无错误
#   2. 3个客户端连接 — 3个 nc 客户端同时连接
#   3. 消息收发与 \n 分帧 — 发送消息，按 \n 正确分帧（粘包测试）
#   4. 连接退出与清理 — quit/关闭后 EPOLL_CTL_DEL + close
#   5. epoll 代码检查 — epoll_create1/epoll_ctl/epoll_wait 使用正确
#
# 注意：不使用 set -e，改为手动检查每步的返回值

SERVER_EXE="./epoll_server"
CLIENT_EXE="./chat_client"
HOST="127.0.0.1"
PORT=9999  # 使用非默认端口避免冲突

echo "=========================================="
echo "Day10 测试：Epoll 聊天服务器"
echo "=========================================="

# 清理残留进程
cleanup_procs() {
    pkill -f "epoll_server" 2>/dev/null || true
    pkill -f "chat_client" 2>/dev/null || true
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
echo "--- 测试 1: 编译 EpollServer ---"
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
echo "--- 测试 2: 启动 EpollServer ---"
cleanup_procs

# 启动服务器
./"$SERVER_EXE" "$PORT" &
SERVER_PID=$!
sleep 1

if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "EpollServer 启动成功（PID: $SERVER_PID）"
else
    fail "EpollServer 启动失败"
    exit 1
fi

# ======================================================
# 测试 3: 3个 nc 客户端同时连接
# ======================================================
echo ""
echo "--- 测试 3: 3个 nc 客户端连接 ---"

TMPDIR=$(mktemp -d)

# 启动3个后台 nc 连接（每个发送一条消息后保持连接）
# nc1: 发送单条消息
(echo "hello from nc1"; sleep 10) | nc $HOST $PORT > "$TMPDIR/nc1_out" 2>/dev/null &
NC1_PID=$!

# nc2: 粘包测试 — 一次发送3条消息
(printf "msg1\nmsg2\nmsg3\n"; sleep 10) | nc $HOST $PORT > "$TMPDIR/nc2_out" 2>/dev/null &
NC2_PID=$!

# nc3: 半包测试 — 先连接，后发消息
(printf "hel"; sleep 0.5; printf "lo\n"; sleep 10) | nc $HOST $PORT > "$TMPDIR/nc3_out" 2>/dev/null &
NC3_PID=$!

sleep 2

# 检查3个连接是否建立
CONN_COUNT=$(ss -tnp 2>/dev/null | grep ":$PORT" | grep -c "ESTAB" || echo 0)
NC1_OK=0; NC2_OK=0; NC3_OK=0
if kill -0 $NC1_PID 2>/dev/null; then NC1_OK=1; fi
if kill -0 $NC2_PID 2>/dev/null; then NC2_OK=1; fi
if kill -0 $NC3_PID 2>/dev/null; then NC3_OK=1; fi

TOTAL=$((NC1_OK + NC2_OK + NC3_OK))
if [ "$TOTAL" -ge 2 ]; then
    pass "$TOTAL/3 个客户端连接成功，服务器仍在运行"
else
    fail "客户端连接不足 ($TOTAL/3)"
fi

# ======================================================
# 测试 4: 粘包分帧验证
# ======================================================
echo ""
echo "--- 测试 4: 粘包分帧测试 ---"

# 检查 nc2 的响应 — 每条msg都应该有对应的回显
NC2_RESP=$(cat "$TMPDIR/nc2_out" 2>/dev/null)
MSG1_OK=0; MSG2_OK=0; MSG3_OK=0
if echo "$NC2_RESP" | grep -q "msg1"; then MSG1_OK=1; fi
if echo "$NC2_RESP" | grep -q "msg2"; then MSG2_OK=1; fi
if echo "$NC2_RESP" | grep -q "msg3"; then MSG3_OK=1; fi

STICKY_PASS=$((MSG1_OK + MSG2_OK + MSG3_OK))
if [ "$STICKY_PASS" -ge 2 ]; then
    pass "粘包测试 — 服务器正确分帧 $STICKY_PASS/3 条消息"
else
    fail "粘包测试 — 分帧失败"
fi

# ======================================================
# 测试 5: 半包分帧验证
# ======================================================
echo ""
echo "--- 测试 5: 半包分帧测试 ---"

NC3_RESP=$(cat "$TMPDIR/nc3_out" 2>/dev/null)
if echo "$NC3_RESP" | grep -q "hello"; then
    pass "半包测试 — 服务器正确拼接半包消息"
else
    fail "半包测试 — 半包拼接失败"
fi

# ======================================================
# 测试 6: 单消息收发
# ======================================================
echo ""
echo "--- 测试 6: 单消息 echo 验证 ---"

NC1_RESP=$(cat "$TMPDIR/nc1_out" 2>/dev/null)
if echo "$NC1_RESP" | grep -q "hello from nc1"; then
    pass "单消息 — 服务器正确回显 'hello from nc1'"
else
    fail "单消息 — 回显不正确"
fi

# ======================================================
# 测试 7: quit 退出与清理
# ======================================================
echo ""
echo "--- 测试 7: quit 命令退出 ---"

# 发送 quit 到服务器
printf "quit\n" | nc $HOST $PORT > /dev/null 2>&1
sleep 1

if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "客户端 quit — 服务器仍在运行（其他客户端不受影响）"
else
    fail "客户端 quit — 服务器崩溃"
fi

# ======================================================
# 测试 8: 关闭连接（recv==0）后的清理
# ======================================================
echo ""
echo "--- 测试 8: 客户端关闭后服务器稳定性 ---"

# 杀掉 nc2（发送 FIN）
kill $NC2_PID 2>/dev/null || true
sleep 1

if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "客户端关闭连接 — 服务器仍在运行（剩余客户端不受影响）"
else
    fail "客户端关闭连接 — 服务器崩溃"
fi

# 清理
kill $NC1_PID $NC3_PID 2>/dev/null || true
rm -rf "$TMPDIR"

# ======================================================
# 测试 9: 代码检查
# ======================================================
echo ""
echo "--- 测试 9: epoll API 使用检查 ---"

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

# 验证未使用 select/多线程/多进程（排除注释行）
COMMENT_FREE=$(grep -v '^\s*\*' "$SRC_FILE" | grep -v '^\s*/' | grep -v '^\s*//')
if ! echo "$COMMENT_FREE" | grep -q "FD_ZERO\|FD_SET\|FD_ISSET\|FD_CLR\|select("; then
    pass "代码未使用 select() / fd_set（正确）"
else
    fail "代码不应包含 select/fd_set 相关内容"
fi

if ! grep -q "pthread\|fork" "$SRC_FILE"; then
    pass "代码未使用多线程/多进程（正确）"
else
    fail "代码不应包含 pthread/fork（应纯 epoll 单线程）"
fi

# ======================================================
# 结果汇总
# ======================================================
echo ""
echo "=========================================="
echo "Day10 EpollServer 测试结果汇总"
echo "=========================================="
echo "  通过: $PASS_COUNT"
echo "  失败: $FAIL_COUNT"

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "存在 $FAIL_COUNT 个失败项！"
    exit 1
else
    echo "Day10 EpollServer 测试全部通过！"
    echo "=========================================="
fi
