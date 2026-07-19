#!/bin/bash
# test_day13.sh - 验证 W3D3 HTTP Search Server V1.3 功能
#
# 对照 W3D3 验收标准:
#   1. GET /search 返回查询表单
#   2. GET /search?class=...&keyword=... 返回查询结果
#   3. POST /search 解析表单请求体并返回相同结果
#   4. UTF-8 / 中文查询 / 错误响应 / 日志 / 统一清理

EXE="./mini_web_server"
HOST="127.0.0.1"
PORT=8080
MAX_REQ=20

echo "=========================================="
echo "Day13 测试：HTTP Search Server V1.3"
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
echo "--- 测试 1: 编译 V1.3 ---"
make clean > /dev/null 2>&1
if make > /dev/null 2>&1; then
    pass "编译成功"
else
    fail "编译失败"
    exit 1
fi

# ======================================================
# 测试 2: 启动
# ======================================================
echo ""
echo "--- 测试 2: 启动 V1.3 ---"
cleanup_procs
rm -f logs/system.log logs/access.log

./"$EXE" serve-http "$MAX_REQ" &
SERVER_PID=$!
sleep 1

if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "V1.3 启动成功"
else
    fail "V1.3 启动失败"
    exit 1
fi

# ======================================================
# 测试 3: GET /search 返回搜索表单
# ======================================================
echo ""
echo "--- 测试 3: GET /search — 搜索表单 ---"

RESP=$(curl -s --max-time 5 "http://${HOST}:${PORT}/search" 2>/dev/null)

if echo "$RESP" | grep -q "学生信息查询"; then
    pass "GET /search 返回搜索表单（含'学生信息查询'）"
else
    fail "GET /search 未返回搜索表单"
fi

if echo "$RESP" | grep -q "GET 查询"; then
    pass "搜索表单包含 GET/POST 查询按钮"
else
    fail "搜索表单缺少查询按钮"
fi

# ======================================================
# 测试 4: GET /search?class=2011&keyword=男 → 查询结果
# ======================================================
echo ""
echo "--- 测试 4: GET 查询 — 中文关键词 ---"

RESP=$(curl -s --max-time 5 \
    "http://${HOST}:${PORT}/search?class=2011&keyword=%E7%94%B7" 2>/dev/null)

if echo "$RESP" | grep -q "查询结果"; then
    pass "GET 查询返回'查询结果'标题"
else
    fail "GET 查询未返回结果页"
fi

if echo "$RESP" | grep -q "张三"; then
    pass "GET 查询匹配到'张三'"
else
    fail "GET 查询未匹配'张三'"
fi

if echo "$RESP" | grep -q "李四"; then
    pass "GET 查询匹配到'李四'"
else
    fail "GET 查询未匹配'李四'"
fi

# 不应出现女生（keyword=男）
if ! echo "$RESP" | grep -q "王五"; then
    pass "GET 查询正确过滤（不含'王五'）"
else
    fail "GET 查询过滤不正确"
fi

# ======================================================
# 测试 5: GET /search?class=2011&keyword=女
# ======================================================
echo ""
echo "--- 测试 5: GET 查询 — 另一个关键词 ---"

RESP=$(curl -s --max-time 5 \
    "http://${HOST}:${PORT}/search?class=2011&keyword=%E5%A5%B3" 2>/dev/null)

if echo "$RESP" | grep -q "王五"; then
    pass "GET 查询 keyword=女 匹配到'王五'"
else
    fail "GET 查询 keyword=女 未匹配"
fi

# ======================================================
# 测试 6: POST /search — 与 GET 相同结果
# ======================================================
echo ""
echo "--- 测试 6: POST 查询 ---"

RESP=$(curl -s --max-time 5 -X POST \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "class=2011&keyword=%E7%94%B7" \
    "http://${HOST}:${PORT}/search" 2>/dev/null)

if echo "$RESP" | grep -q "查询结果"; then
    pass "POST /search 返回结果页"
else
    fail "POST /search 未返回结果页"
fi

if echo "$RESP" | grep -q "张三"; then
    pass "POST 查询匹配到'张三'（与 GET 结果一致）"
else
    fail "POST 查询未匹配"
fi

if echo "$RESP" | grep -q "李四"; then
    pass "POST 查询匹配到'李四'"
else
    fail "POST 查询未匹配'李四'"
fi

# ======================================================
# 测试 7: 无匹配结果
# ======================================================
echo ""
echo "--- 测试 7: 无匹配结果 ---"

# 赵七 — 不存在的学生
RESP=$(curl -s --max-time 5 \
    "http://${HOST}:${PORT}/search?class=2011&keyword=%E8%B5%B5%E4%B8%83" 2>/dev/null)

if echo "$RESP" | grep -q "未找到符合条件的记录"; then
    pass "无匹配时显示'未找到符合条件的记录'"
else
    fail "无匹配时未显示提示"
fi

# ======================================================
# 测试 8: 班级格式错误 → 400
# ======================================================
echo ""
echo "--- 测试 8: 参数错误 → 400 ---"

RESP=$(curl -s --max-time 5 \
    "http://${HOST}:${PORT}/search?class=abc&keyword=男" 2>/dev/null)

if echo "$RESP" | grep -q "班级格式错误"; then
    pass "class=abc 返回'班级格式错误'"
else
    fail "class=abc 未正确报错"
fi

# class 不足 4 位
RESP=$(curl -s --max-time 5 \
    "http://${HOST}:${PORT}/search?class=20&keyword=男" 2>/dev/null)

if echo "$RESP" | grep -q "班级格式错误\|400"; then
    pass "class=20（不足4位）返回错误"
else
    fail "class=20 未正确报错"
fi

# ======================================================
# 测试 9: 数据文件不存在 → 404
# ======================================================
echo ""
echo "--- 测试 9: 数据不存在 → 404 ---"

RESP=$(curl -s --max-time 5 \
    "http://${HOST}:${PORT}/search?class=9999&keyword=男" 2>/dev/null)

if echo "$RESP" | grep -q "班级数据不存在"; then
    pass "class=9999 返回'班级数据不存在'"
else
    fail "class=9999 未正确返回 404"
fi

# ======================================================
# 测试 10: 缺少参数 → 400
# ======================================================
echo ""
echo "--- 测试 10: 缺少参数 ---"

RESP=$(curl -s --max-time 5 \
    "http://${HOST}:${PORT}/search?class=2011" 2>/dev/null)

if echo "$RESP" | grep -q "参数格式错误\|400"; then
    pass "缺少 keyword 参数返回错误"
else
    fail "缺少 keyword 参数未报错"
fi

# ======================================================
# 测试 11: POST 缺少 Content-Type → 415
# ======================================================
echo ""
echo "--- 测试 11: POST 缺少 Content-Type → 415 ---"

RESP=$(curl -s --max-time 5 -X POST \
    -d "class=2011&keyword=男" \
    "http://${HOST}:${PORT}/search" 2>/dev/null)

# curl without -H sends Content-Type: application/x-www-form-urlencoded by default
# So we need to explicitly send wrong type
RESP=$(curl -s --max-time 5 -X POST \
    -H "Content-Type: text/plain" \
    -d "class=2011&keyword=男" \
    "http://${HOST}:${PORT}/search" 2>/dev/null)

if echo "$RESP" | grep -q "415\|Unsupported"; then
    pass "错误 Content-Type 返回 415"
else
    fail "错误 Content-Type 未返回 415"
fi

# ======================================================
# 测试 12: 请求体过大 → 413
# ======================================================
echo ""
echo "--- 测试 12: 请求体过大 → 413 ---"

BIG_DATA="class=2011&keyword="
for i in $(seq 1 4100); do BIG_DATA="${BIG_DATA}x"; done

RESP=$(curl -s --max-time 5 -X POST \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "$BIG_DATA" \
    "http://${HOST}:${PORT}/search" 2>/dev/null)

if echo "$RESP" | grep -q "413\|Payload Too Large"; then
    pass "超大请求体返回 413"
else
    fail "超大请求体未返回 413"
fi

# ======================================================
# 测试 13: 405 — /search 仅允许 GET 和 POST
# ======================================================
echo ""
echo "--- 测试 13: 405 方法检查 ---"

RESP=$(curl -s --max-time 5 -X PUT \
    -d "class=2011" \
    "http://${HOST}:${PORT}/search" 2>/dev/null)

if echo "$RESP" | grep -q "405"; then
    pass "PUT /search 返回 405"
else
    fail "PUT /search 未返回 405"
fi

# ======================================================
# 测试 14: V1.2 静态资源仍可访问
# ======================================================
echo ""
echo "--- 测试 14: V1.2 静态资源兼容 ---"

RESP=$(curl -s --max-time 5 "http://${HOST}:${PORT}/" 2>/dev/null)
if echo "$RESP" | grep -q "Hello, HTTP!"; then
    pass "GET / 静态首页正常（V1.2 兼容）"
else
    fail "GET / 静态首页失败"
fi

RESP=$(curl -s --max-time 5 "http://${HOST}:${PORT}/css/style.css" 2>/dev/null)
if echo "$RESP" | grep -q "MiniWeb"; then
    pass "GET /css/style.css 静态资源正常"
else
    fail "GET /css/style.css 失败"
fi

# ======================================================
# 测试 15: V1.1 POST /echo 兼容
# ======================================================
echo ""
echo "--- 测试 15: V1.1 兼容 ---"

RESP=$(curl -s --max-time 5 -X POST -d "hello" \
    "http://${HOST}:${PORT}/echo" 2>/dev/null)
if echo "$RESP" | grep -q "Echo"; then
    pass "POST /echo 仍然正常（V1.1 兼容）"
else
    fail "POST /echo 失败"
fi

# ======================================================
# 测试 16: 目录穿越防护
# ======================================================
echo ""
echo "--- 测试 16: 目录穿越 ---"

RESP=$(curl -s --max-time 5 --path-as-is \
    "http://${HOST}:${PORT}/../etc/passwd" 2>/dev/null)
if echo "$RESP" | grep -q "403"; then
    pass "/../etc/passwd 返回 403（V1.2 兼容）"
else
    fail "/../etc/passwd 未返回 403"
fi

# ======================================================
# 测试 17: 查询处理器代码检查
# ======================================================
echo ""
echo "--- 测试 17: query_handler 模块 ---"

if [ -f "src/query_handler.c" ] && [ -f "include/query_handler.h" ]; then
    pass "query_handler.h/c 模块存在"
else
    fail "query_handler 模块缺失"
fi

if grep -q "url_decode" src/query_handler.c; then
    pass "包含 url_decode() URL 解码函数"
else
    fail "缺少 url_decode()"
fi

if grep -q "parse_query_string" src/query_handler.c; then
    pass "包含 parse_query_string() 查询串解析"
else
    fail "缺少 parse_query_string()"
fi

if grep -q "html_escape" src/query_handler.c; then
    pass "包含 html_escape() HTML 转义函数"
else
    fail "缺少 html_escape()"
fi

if grep -q "validate_class\|validate_keyword" src/query_handler.c; then
    pass "包含参数校验函数"
else
    fail "缺少参数校验函数"
fi

if grep -q "query_records" src/query_handler.c; then
    pass "包含 query_records() 数据查询函数"
else
    fail "缺少 query_records()"
fi

# ======================================================
# 测试 18: epoll API 检查
# ======================================================
echo ""
echo "--- 测试 18: epoll API ---"

if grep -q "epoll_create1\|epoll_ctl\|epoll_wait" src/http_server.c; then
    pass "http_server.c 仍使用 epoll API"
else
    fail "epoll API 缺失"
fi

if grep -q "EPOLL_CTL_DEL" src/http_server.c && grep -q "close" src/http_server.c; then
    pass "仍包含 EPOLL_CTL_DEL + close 清理"
else
    fail "连接清理缺失"
fi

# ======================================================
# 测试 19: 数据文件查询多个班级
# ======================================================
echo ""
echo "--- 测试 19: 不同班级数据 ---"

RESP=$(curl -s --max-time 5 \
    "http://${HOST}:${PORT}/search?class=2012&keyword=%E5%B0%8F" 2>/dev/null)

if echo "$RESP" | grep -q "小明"; then
    pass "class=2012 查询匹配到'小明'"
else
    fail "class=2012 查询失败"
fi

if echo "$RESP" | grep -q "小芳"; then
    pass "class=2012 查询匹配到'小芳'"
else
    fail "class=2012 查询未匹配'小芳'"
fi

# ======================================================
# 测试 20: 日志检查
# ======================================================
echo ""
echo "--- 测试 20: 日志 ---"

if [ -f "logs/system.log" ]; then
    pass "系统日志存在"
else
    fail "系统日志不存在"
fi

if [ -f "logs/access.log" ]; then
    pass "访问日志存在"
else
    fail "访问日志不存在"
fi

if grep -q "/search" logs/access.log 2>/dev/null; then
    pass "访问日志包含 /search 记录"
else
    fail "访问日志缺少 /search 记录"
fi

if grep -q "search query" logs/system.log 2>/dev/null; then
    pass "系统日志包含查询记录"
else
    fail "系统日志缺少查询记录"
fi

# ======================================================
# 结果汇总
# ======================================================
echo ""
echo "=========================================="
echo "Day13 HTTP Search Server V1.3 测试结果汇总"
echo "=========================================="
echo "  通过: $PASS_COUNT"
echo "  失败: $FAIL_COUNT"

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "存在 $FAIL_COUNT 个失败项！"
    exit 1
else
    echo "Day13 HTTP Search Server V1.3 测试全部通过！"
fi
