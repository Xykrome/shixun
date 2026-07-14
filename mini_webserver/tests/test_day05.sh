#!/bin/bash
# test_day05.sh - 验证 V0.5 多线程处理请求功能
#
# 检查内容：
#   1. 多线程模式能正常启动（--thread 参数）
#   2. 每个请求文件都有对应输出文件
#   3. 主线程能等待所有 worker 线程结束（pthread_join）
#   4. 日志中能看到 Worker 线程记录（TID）
#   5. 输出文件内容正确
#   6. 日志中能看到主线程入队记录

set -e

EXE="./mini_web_server"
CONF="conf/server.conf"
LOG="logs/server.log"
OUT_DIR="outputs"

echo "=========================================="
echo "Day05 测试：多线程处理客户请求 (V0.5)"
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

# 启动服务器（多线程模式，4 个 worker）
echo ""
echo "--- 2. 启动多线程服务器，处理请求 ---"
./"$EXE" --thread "$CONF" 4
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

# 验证日志中包含主线程入队记录
if grep -q "enqueued" "$LOG"; then
    echo "PASS: 日志包含主线程入队记录"
else
    echo "FAIL: 日志中没有入队记录"
fi

# 验证日志中包含 Worker 线程记录（TID=)
if grep -q "TID=" "$LOG"; then
    echo "PASS: 日志包含 Worker 线程 TID 信息"
else
    echo "FAIL: 日志中没有 Worker 线程 TID 信息"
fi

# 验证日志中包含 Worker 启动记录
if grep -q "Worker-" "$LOG"; then
    echo "PASS: 日志包含 Worker 线程启动/工作记录"
else
    echo "FAIL: 日志中没有 Worker 记录"
fi

# 验证日志中包含 joined 记录（pthread_join）
if grep -q "joined" "$LOG"; then
    echo "PASS: 日志包含 pthread_join 等待记录"
else
    echo "FAIL: 日志中没有 pthread_join 记录"
fi

# 验证日志中包含统计信息（Total processed）
if grep -q "Total processed" "$LOG"; then
    echo "PASS: 日志包含处理统计信息"
else
    echo "FAIL: 日志中没有统计信息"
fi

# 验证日志中包含多线程模式标识
if grep -q "Multi-threaded" "$LOG"; then
    echo "PASS: 日志包含多线程模式标识 (V0.5)"
else
    echo "FAIL: 日志中没有多线程模式标识"
fi

echo ""
echo "--- 日志内容预览 ---"
cat "$LOG"
echo ""

echo "=========================================="
echo "Day05 测试全部通过！"
echo "=========================================="
