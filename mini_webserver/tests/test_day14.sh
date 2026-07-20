#!/bin/bash
#
# test_day14.sh — Webserver V1.4 配置驱动服务器 自动化测试
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

SERVER="$PROJECT_DIR/mini_web_server"
PORT=8080
PASS=0
FAIL=0

# ── helpers ──────────────────────────────────────────────────────────────

# Start server, send exactly N requests, then wait for exit.
# $1 = config file, $2 = max_requests, $3... = curl args to run $2 times
run_server() {
    local config="$1"
    local max_req="$2"
    shift 2
    pkill -f mini_web_server 2>/dev/null || true
    sleep 0.3
    rm -f logs/server.log logs/server_access.log 2>/dev/null
    "$SERVER" "$config" "$max_req" &
    local pid=$!
    sleep 1
    # Execute curl commands passed as remaining args
    for cmd in "$@"; do
        eval "$cmd" > /dev/null 2>&1 || true
    done
    wait $pid 2>/dev/null || true
}

check_pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
check_fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

# Clean up any stale servers
pkill -f mini_web_server 2>/dev/null || true
sleep 0.5

# ── 1. 编译 ──────────────────────────────────────────────────────────────

echo "=== Day14 V1.4 配置驱动服务器 测试开始 ==="
echo ""

echo "--- 测试 1: 编译 ---"
if make -j4 2>&1 | grep -q "Error"; then
    check_fail "编译失败"
else
    check_pass "编译成功"
fi

echo "--- 测试 1b: 二进制文件检查 ---"
if [ -x "$SERVER" ]; then
    check_pass "mini_web_server 可执行文件存在"
else
    check_fail "mini_web_server 未构建"
fi

# ── 2. 配置文件解析 ──────────────────────────────────────────────────────

echo ""
echo "--- 测试 2: JSON 配置文件加载 ---"
rm -f logs/server.log logs/server_access.log 2>/dev/null
"$SERVER" config/server.json 2 > /tmp/v14_test.log 2>&1 &
PID=$!
sleep 1
curl -s "http://127.0.0.1:$PORT/search" > /dev/null 2>&1 || true
curl -s "http://127.0.0.1:$PORT/" > /dev/null 2>&1 || true
wait $PID 2>/dev/null || true

grep -q "W3D4 HTTP Server V1.4" /tmp/v14_test.log && check_pass "服务器识别为 V1.4" || check_fail "服务器未显示 V1.4 标识"
grep -q "server.host.*127.0.0.1" /tmp/v14_test.log && check_pass "配置 host 正确读取" || check_fail "配置 host 读取错误"
grep -q "server.port.*8080" /tmp/v14_test.log && check_pass "配置 port 正确读取" || check_fail "配置 port 读取错误"
grep -q "document_root.*www" /tmp/v14_test.log && check_pass "配置 document_root 正确读取" || check_fail "配置 document_root 读取错误"
grep -q "Routes: 10" /tmp/v14_test.log && check_pass "路由表加载：10 条路由" || check_fail "路由表加载错误"

# ── 3. 配置端口修改 ──────────────────────────────────────────────────────

echo ""
echo "--- 测试 3: 修改端口无需重新编译 ---"
cat > /tmp/v14_port_test.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":9090,"document_root":"./www"},"logging":{"level":"INFO","file":"./logs/server.log"},"routes":[]}
EOFJSON
timeout 2 "$SERVER" /tmp/v14_port_test.json 1 > /tmp/v14_port.log 2>&1 &
PID=$!
sleep 1
curl -s "http://127.0.0.1:9090/" > /dev/null 2>&1 || true
wait $PID 2>/dev/null || true
grep -q "bind.*9090" /tmp/v14_port.log && check_pass "端口 8080 → 9090，bind() 正确" || check_fail "端口修改未生效"

# ── 4. 配置校验 — port 范围 ──────────────────────────────────────────────

echo ""
echo "--- 测试 4: 配置校验 — port 范围 ---"
cat > /tmp/v14_bad_port.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":0,"document_root":"./www"},"logging":{"level":"INFO","file":"./logs/server.log"},"routes":[]}
EOFJSON
"$SERVER" /tmp/v14_bad_port.json 2 2>&1 | grep -q "out of range" && check_pass "port=0 被拒绝" || check_fail "port=0 未被拒绝"

cat > /tmp/v14_bad_port2.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":99999,"document_root":"./www"},"logging":{"level":"INFO","file":"./logs/server.log"},"routes":[]}
EOFJSON
"$SERVER" /tmp/v14_bad_port2.json 2 2>&1 | grep -q "out of range" && check_pass "port=99999 被拒绝" || check_fail "port=99999 未被拒绝"

# ── 5. 配置校验 — log level ─────────────────────────────────────────────

echo ""
echo "--- 测试 5: 配置校验 — log level ---"
cat > /tmp/v14_bad_level.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":8080,"document_root":"./www"},"logging":{"level":"TRACE","file":"./logs/server.log"},"routes":[]}
EOFJSON
"$SERVER" /tmp/v14_bad_level.json 2 2>&1 | grep -q "DEBUG|INFO|WARN|ERROR" && check_pass "非法日志级别被拒绝" || check_fail "非法日志级别未被拒绝"

# ── 6. 配置校验 — handler 名称 ──────────────────────────────────────────

echo ""
echo "--- 测试 6: 配置校验 — handler 名称 ---"
cat > /tmp/v14_bad_handler.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":8080,"document_root":"./www"},"logging":{"level":"INFO","file":"./logs/server.log"},"routes":[{"method":"GET","path":"/x","handler":"no_such_handler"}]}
EOFJSON
"$SERVER" /tmp/v14_bad_handler.json 2 2>&1 | grep -q "not a registered handler" && check_pass "未注册的 handler 被拒绝" || check_fail "未注册的 handler 未被拒绝"

# ── 7. 配置校验 — 重复路由 ─────────────────────────────────────────────

echo ""
echo "--- 测试 7: 配置校验 — 重复路由 ---"
cat > /tmp/v14_dup.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":8080,"document_root":"./www"},"logging":{"level":"INFO","file":"./logs/server.log"},"routes":[{"method":"GET","path":"/search","handler":"search_get"},{"method":"GET","path":"/search","handler":"search_get"}]}
EOFJSON
"$SERVER" /tmp/v14_dup.json 2 2>&1 | grep -q "duplicate route" && check_pass "重复路由被拒绝" || check_fail "重复路由未被拒绝"

# ── 8. 配置校验 — path 必须以 / 开头 ────────────────────────────────────

echo ""
echo "--- 测试 8: 配置校验 — path 必须以 / 开头 ---"
cat > /tmp/v14_bad_path.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":8080,"document_root":"./www"},"logging":{"level":"INFO","file":"./logs/server.log"},"routes":[{"method":"GET","path":"search","handler":"search_get"}]}
EOFJSON
"$SERVER" /tmp/v14_bad_path.json 2 2>&1 | grep -q "must start with '/'" && check_pass "path 不以 / 开头被拒绝" || check_fail "path 不以 / 开头未被拒绝"

# ── 9. 配置校验 — document_root 不存在 ──────────────────────────────────

echo ""
echo "--- 测试 9: 配置校验 — document_root ---"
cat > /tmp/v14_bad_root.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":8080,"document_root":"./no_such_dir"},"logging":{"level":"INFO","file":"./logs/server.log"},"routes":[]}
EOFJSON
"$SERVER" /tmp/v14_bad_root.json 2 2>&1 | grep -qE "not a directory|does not exist" && check_pass "非法 document_root 被拒绝" || check_fail "非法 document_root 未被拒绝"

# ── 10. 配置校验 — 缺少必填字段 ─────────────────────────────────────────

echo ""
echo "--- 测试 10: 配置校验 — 缺少字段 ---"
cat > /tmp/v14_missing.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":8080,"document_root":"./www"}}
EOFJSON
"$SERVER" /tmp/v14_missing.json 2 2>&1 | grep -qE "missing|invalid|FATAL" && check_pass "缺少必填字段被拒绝" || check_fail "缺少必填字段未被拒绝"

# ── 11. 配置校验 — JSON 语法错误 ────────────────────────────────────────

echo ""
echo "--- 测试 11: 配置校验 — JSON 语法错误 ---"
echo "this is not json at all" > /tmp/v14_bad_json.json
"$SERVER" /tmp/v14_bad_json.json 2 2>&1 | grep -qE "parse error|syntax" && check_pass "JSON 语法错误被拒绝" || check_fail "JSON 语法错误未被拒绝"

# ── 12. GET /search 路由分发 ────────────────────────────────────────────

echo ""
echo "--- 测试 12: 路由分发 — GET /search ---"
rm -f logs/server.log logs/server_access.log
"$SERVER" config/server.json 1 > /tmp/v14_t12.log 2>&1 &
PID=$!
sleep 1
RESP=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/search" 2>/dev/null || echo "000")
wait $PID 2>/dev/null || true
[ "$RESP" = "200" ] && check_pass "GET /search → 200" || check_fail "GET /search → $RESP (expected 200)"

# ── 13. POST /search 路由分发 ───────────────────────────────────────────

echo ""
echo "--- 测试 13: 路由分发 — POST /search ---"
rm -f logs/server.log logs/server_access.log
"$SERVER" config/server.json 1 > /tmp/v14_t13.log 2>&1 &
PID=$!
sleep 1
RESP=$(curl -s -o /dev/null -w "%{http_code}" \
  -X POST -H "Content-Type: application/x-www-form-urlencoded" \
  -d "class=2011&keyword=男" \
  "http://127.0.0.1:$PORT/search" 2>/dev/null || echo "000")
wait $PID 2>/dev/null || true
[ "$RESP" = "200" ] && check_pass "POST /search → 200" || check_fail "POST /search → $RESP (expected 200)"

# ── 14. GET / (首页静态文件) ─────────────────────────────────────────────

echo ""
echo "--- 测试 14: 静态文件 — GET / ---"
rm -f logs/server.log logs/server_access.log
"$SERVER" config/server.json 1 > /tmp/v14_t14.log 2>&1 &
PID=$!
sleep 1
RESP=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/" 2>/dev/null || echo "000")
wait $PID 2>/dev/null || true
[ "$RESP" = "200" ] && check_pass "GET / (index.html) → 200" || check_fail "GET / (index.html) → $RESP (expected 200)"

# ── 15. 405 — path 在路由表但 method 不匹配 ──────────────────────────────

echo ""
echo "--- 测试 15: 405 — method not allowed ---"
rm -f logs/server.log logs/server_access.log
"$SERVER" config/server.json 2 > /tmp/v14_t15.log 2>&1 &
PID=$!
sleep 1
FULL=$(curl -s -i -X PUT "http://127.0.0.1:$PORT/search" 2>/dev/null || echo "")
RESP=$(echo "$FULL" | head -1 | grep -oP '\d{3}' | head -1)
ALLOW=$(echo "$FULL" | grep -i "^Allow:" || echo "")
# Consume remaining request
curl -s -X PUT "http://127.0.0.1:$PORT/search" > /dev/null 2>&1 || true
wait $PID 2>/dev/null || true
[ "$RESP" = "405" ] && check_pass "PUT /search → 405" || check_fail "PUT /search → $RESP (expected 405)"
echo "$ALLOW" | grep -q "GET.*POST" && check_pass "405 Allow 头包含 GET, POST" || check_fail "405 Allow 头不正确: $ALLOW"

# ── 16. 404 — 不存在的静态文件 ──────────────────────────────────────────

echo ""
echo "--- 测试 16: 404 — 资源不存在 ---"
rm -f logs/server.log logs/server_access.log
"$SERVER" config/server.json 1 > /tmp/v14_t16.log 2>&1 &
PID=$!
sleep 1
RESP=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/nonexistent.html" 2>/dev/null || echo "000")
wait $PID 2>/dev/null || true
[ "$RESP" = "404" ] && check_pass "GET /nonexistent.html → 404" || check_fail "GET /nonexistent.html → $RESP (expected 404)"

# ── 17. GET /search?class=xxx&keyword=yyy → 200 ─────────────────────────

echo ""
echo "--- 测试 17: GET /search 带查询参数 ---"
rm -f logs/server.log logs/server_access.log
"$SERVER" config/server.json 1 > /tmp/v14_t17.log 2>&1 &
PID=$!
sleep 1
FULL=$(curl -s -i "http://127.0.0.1:$PORT/search?class=2011&keyword=%E7%94%B7" 2>/dev/null || echo "")
RESP=$(echo "$FULL" | head -1 | grep -oP '\d{3}' | head -1)
BODY=$(echo "$FULL")
wait $PID 2>/dev/null || true
[ "$RESP" = "200" ] && check_pass "GET /search?class=2011&keyword=男 → 200" || check_fail "GET /search?... → $RESP (expected 200)"
echo "$BODY" | grep -q "张三" && check_pass "查询结果包含'张三'" || check_fail "查询结果未找到'张三'"

# ── 18. POST /echo (V1.1 兼容) ───────────────────────────────────────────

echo ""
echo "--- 测试 18: POST /echo (V1.1) ---"
rm -f logs/server.log logs/server_access.log
"$SERVER" config/server.json 1 > /tmp/v14_t18.log 2>&1 &
PID=$!
sleep 1
RESP=$(curl -s -X POST -d "test_data" "http://127.0.0.1:$PORT/echo" 2>/dev/null || echo "")
wait $PID 2>/dev/null || true
echo "$RESP" | grep -q "Echo: test_data" && check_pass "POST /echo 路由正确 → 'Echo: test_data'" || check_fail "POST /echo 路由错误: $RESP"

# ── 19. V1.3 向后兼容 — serve-http ──────────────────────────────────────

echo ""
echo "--- 测试 19: V1.3 向后兼容 ---"
timeout 2 "$SERVER" serve-http 1 > /tmp/v14_compat.log 2>&1 &
PID=$!
sleep 1
curl -s "http://127.0.0.1:8080/" > /dev/null 2>&1 || true
wait $PID 2>/dev/null || true
grep -q "V1.3 epoll" /tmp/v14_compat.log && check_pass "serve-http 模式下 V1.3 标记存在" || check_fail "serve-http 模式 V1.3 标记丢失"

# ── 20. 日志 ─────────────────────────────────────────────────────────────

echo ""
echo "--- 测试 20: 日志系统 ---"
rm -f logs/server.log logs/server_access.log
"$SERVER" config/server.json 2 > /tmp/v14_t20.log 2>&1 &
PID=$!
sleep 1
curl -s "http://127.0.0.1:$PORT/search?class=2011&keyword=男" > /dev/null 2>&1 || true
curl -s -X POST -H "Content-Type: application/x-www-form-urlencoded" \
  -d "class=2011&keyword=女" "http://127.0.0.1:$PORT/search" > /dev/null 2>&1 || true
wait $PID 2>/dev/null || true

[ -f logs/server.log ] && check_pass "系统日志存在" || check_fail "系统日志不存在"
[ -f logs/server_access.log ] && check_pass "访问日志存在" || check_fail "访问日志不存在"
grep -q "/search" logs/server_access.log 2>/dev/null && check_pass "访问日志包含 /search 记录" || check_fail "访问日志缺少 /search 记录"

# ── 21. 配置启动后不进入监听 ────────────────────────────────────────────

echo ""
echo "--- 测试 21: 配置错误时拒绝启动 ---"
"$SERVER" /tmp/v14_bad_handler.json 2 2>&1 | grep -qE "FATAL|not a registered" && check_pass "配置错误 → 打印 FATAL，拒绝启动" || check_fail "配置错误 → 未拒绝启动"

# ── 22. 多请求连续处理 ──────────────────────────────────────────────────

echo ""
echo "--- 测试 22: 多请求连续处理 ---"
rm -f logs/server.log logs/server_access.log
"$SERVER" config/server.json 3 > /tmp/v14_t22.log 2>&1 &
PID=$!
sleep 1
R1=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/" 2>/dev/null || echo "000")
R2=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/search?class=2011" 2>/dev/null || echo "000")
R3=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/search?keyword=小" 2>/dev/null || echo "000")
wait $PID 2>/dev/null || true
[ "$R1" = "200" ] && [ "$R2" = "200" ] && [ "$R3" = "200" ] && check_pass "连续 3 个不同类型请求全部 200" || check_fail "连续请求失败: $R1 $R2 $R3"

# ── Summary ──────────────────────────────────────────────────────────────

echo ""
echo "=========================================="
echo "Day14 V1.4 Config-Driven Server 测试结果汇总"
echo "=========================================="
echo "  通过: $PASS"
echo "  失败: $FAIL"

if [ "$FAIL" -eq 0 ]; then
    echo "Day14 V1.4 配置驱动服务器测试全部通过！"
    exit 0
else
    echo "存在 $FAIL 个失败项，请检查。"
    exit 1
fi
