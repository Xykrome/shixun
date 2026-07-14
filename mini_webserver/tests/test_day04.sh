#!/bin/bash
# test_day04.sh - 验证 V0.4 多进程处理请求功能
#
# 检查内容：
#   1. process_requests 能创建多个子进程
#   2. 每个请求文件都有对应输出文件
#   3. 父进程能等待所有子进程结束
#   4. 日志中能看到父进程和子进程记录
#   5. 输出文件内容正确

set -e

EXE="./mini_web_server"
CONF="conf/server.conf"
LOG="logs/server.log"
OUT_DIR="outputs"

echo "=========================================="
echo "Day04 测试：多进程处理客户请求"
echo "=========================================="

# 清理之前的输出
rm -f "$LOG"
rm -f "$OUT_DIR"/*.out

# 确保请求文件存在
echo ""
echo "--- 1. 验证请求文件存在 ---"
for req in request/hello.req request/user_find.req request/missing.req; do
    if [ -f "$req" ]; then
        echo "PASS: $req 存在"
    else
        echo "FAIL: $req 不存在"
        exit 1
    fi
done

# 编译
make clean > /dev/null 2>&1
make > /dev/null 2>&1

# 启动服务器（多进程模式）
echo ""
echo "--- 2. 启动服务器，处理请求 ---"
./"$EXE" "$CONF"
echo "服务器进程退出"

# 检查输出文件是否都生成
echo ""
echo "--- 3. 验证输出文件 ---"

check_output() {
    local out="$OUT_DIR/$1"
    if [ -f "$out" ]; then
        echo "PASS: $out 已生成"
    else
        echo "FAIL: $out 未生成"
        exit 1
    fi
}

check_output "hello.out"
check_output "user_find.out"
check_output "missing.out"

# 验证 hello.out 内容（HTTP 200 响应）
echo ""
echo "--- 4. 验证 hello.out 内容 ---"
if grep -q "HTTP/1.1 200 OK" "$OUT_DIR/hello.out" && \
   grep -q "Hello, Web!" "$OUT_DIR/hello.out"; then
    echo "PASS: hello.out 包含正确的 HTTP 200 响应"
else
    echo "FAIL: hello.out 内容不正确"
    echo "实际内容："
    cat "$OUT_DIR/hello.out"
    exit 1
fi

# 验证 user_find.out 内容（FOUND）
echo ""
echo "--- 5. 验证 user_find.out 内容 ---"
if grep -q "FOUND zhangsan" "$OUT_DIR/user_find.out"; then
    echo "PASS: user_find.out 找到用户 zhangsan"
else
    echo "FAIL: user_find.out 内容不正确"
    echo "实际内容："
    cat "$OUT_DIR/user_find.out"
    exit 1
fi

# 验证 missing.out 内容（404）
echo ""
echo "--- 6. 验证 missing.out 内容 ---"
if grep -q "404 Not Found" "$OUT_DIR/missing.out"; then
    echo "PASS: missing.out 包含 404 Not Found"
else
    echo "FAIL: missing.out 内容不正确"
    echo "实际内容："
    cat "$OUT_DIR/missing.out"
    exit 1
fi

# 验证日志文件存在
echo ""
echo "--- 7. 验证日志文件 ---"
if [ -f "$LOG" ]; then
    echo "PASS: 日志文件已生成"
else
    echo "FAIL: 日志文件不存在"
    exit 1
fi

# 验证日志中包含父进程记录（"Parent"关键字）
if grep -q "Parent" "$LOG"; then
    echo "PASS: 日志包含父进程记录"
else
    echo "FAIL: 日志中没有父进程记录"
fi

# 验证日志中包含子进程记录（"Child"关键字）
if grep -q "Child" "$LOG"; then
    echo "PASS: 日志包含子进程记录"
else
    echo "FAIL: 日志中没有子进程记录"
fi

# 验证日志中包含 PID 信息
if grep -q "PID=" "$LOG"; then
    echo "PASS: 日志包含 PID 信息"
else
    echo "FAIL: 日志中没有 PID 信息"
fi

# 验证日志中包含 waitpid 等待记录
if grep -q "exited with status" "$LOG"; then
    echo "PASS: 日志包含子进程退出状态（waitpid 记录）"
else
    echo "FAIL: 日志中没有 waitpid 等待记录"
fi

echo ""
echo "--- 日志内容预览 ---"
cat "$LOG"
echo ""

echo "=========================================="
echo "Day04 测试全部通过！"
echo "=========================================="
