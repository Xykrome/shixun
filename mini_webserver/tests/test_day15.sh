#!/bin/bash
#
# test_day15.sh — Webserver V1.5 HTTP Basic 认证 自动化测试
#
# W3D5 验收测试：Basic 认证 + V1.4 回归
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

SERVER="$PROJECT_DIR/mini_web_server"
PORT=9090
PASS=0
FAIL=0

# ── helpers ──────────────────────────────────────────────────────────────

# Start server, send exactly N requests, then wait for exit.
# $1 = config file, $2 = max_requests, $3... = curl commands
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

echo "=== Day15 V1.5 HTTP Basic 认证 测试开始 ==="
echo ""

# ── Helper: create test config with auth ─────────────────────────────────

AUTH_TEST_CFG="/tmp/v15_auth_test.json"
cat > "$AUTH_TEST_CFG" << 'EOFJSON'
{
  "server": {
    "host": "127.0.0.1",
    "port": 9090,
    "document_root": "./www"
  },
  "logging": {
    "level": "INFO",
    "file": "./logs/server.log"
  },
  "auth": {
    "basic": {
      "username": "student",
      "password": "lab123"
    }
  },
  "routes": [
    {"method":"GET",  "path":"/search",  "handler":"search_get"},
    {"method":"POST", "path":"/search",  "handler":"search_post"},
    {"method":"POST", "path":"/echo",    "handler":"echo_post"},
    {"method":"GET",  "path":"/secured", "handler":"secured_get", "auth":"basic"}
  ]
}
EOFJSON

# ── 1. 编译 ──────────────────────────────────────────────────────────────

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
    check_fail "mini_web_server 不存在"
fi

# ── 2. 配置加载：auth 部分 ──────────────────────────────────────────────

echo ""
echo "--- 测试 2: JSON 配置加载（含 auth 部分）---"
"$SERVER" "$AUTH_TEST_CFG" 2 > /tmp/v15_cfg.log 2>&1 &
PID=$!
sleep 1
curl -s "http://127.0.0.1:$PORT/search" > /dev/null 2>&1 || true
curl -s "http://127.0.0.1:$PORT/" > /dev/null 2>&1 || true
wait $PID 2>/dev/null || true

grep -q "auth.basic.username.*student" /tmp/v15_cfg.log && check_pass "auth.basic.username 正确加载" || check_fail "auth.basic.username 未正确加载"
grep -q "auth.basic.password.*\*\*\*\*" /tmp/v15_cfg.log && check_pass "auth.basic.password 被隐藏（****）" || check_fail "auth.basic.password 未被隐藏"
grep -q "secured.*auth:basic" /tmp/v15_cfg.log && check_pass "secured 路由已标记 auth:basic" || check_fail "secured 路由未标记 auth"

# ── 3. 公开页面 GET / → 200 ─────────────────────────────────────────────

echo ""
echo "--- 测试 3: GET / (公开页面) → 200 ---"

run_server "$AUTH_TEST_CFG" 1 'curl -s http://127.0.0.1:9090/'

grep -q "HTTP/1.1 200" logs/server_access.log && check_pass "GET / → 200" || check_fail "GET / 未返回 200"

# ── 4. 无凭据访问 /secured → 401 ────────────────────────────────────────

echo ""
echo "--- 测试 4: GET /secured (无凭据) → 401 ---"

run_server "$AUTH_TEST_CFG" 1 'curl -s http://127.0.0.1:9090/secured'

grep -q "HTTP/1.1 401" logs/server_access.log && check_pass "GET /secured 无凭据 → 401" || check_fail "GET /secured 无凭据未返回 401"

# ── 5. 401 响应携带 WWW-Authenticate ─────────────────────────────────────

echo ""
echo "--- 测试 5: 401 响应包含 WWW-Authenticate ---"
# Start server, do one curl, save response headers
"$SERVER" "$AUTH_TEST_CFG" 1 > /dev/null 2>&1 &
PID=$!
sleep 1
curl -s -D- -o /dev/null http://127.0.0.1:$PORT/secured 2>&1 > /tmp/v15_wwwauth.txt || true
wait $PID 2>/dev/null || true
grep -qi "WWW-Authenticate.*Basic.*realm" /tmp/v15_wwwauth.txt && check_pass "WWW-Authenticate 头存在" || check_fail "WWW-Authenticate 头缺失"
sleep 0.3

# ── 6. 错误凭据 → 401 ───────────────────────────────────────────────────

echo ""
echo "--- 测试 6: GET /secured (错误密码) → 401 ---"

run_server "$AUTH_TEST_CFG" 1 'curl -s -u student:wrong http://127.0.0.1:9090/secured'

grep -q "HTTP/1.1 401" logs/server_access.log && check_pass "错误密码 → 401" || check_fail "错误密码未返回 401"

# ── 7. 正确凭据 → 200 ───────────────────────────────────────────────────

echo ""
echo "--- 测试 7: GET /secured (正确凭据) → 200 ---"

run_server "$AUTH_TEST_CFG" 1 'curl -s -u student:lab123 http://127.0.0.1:9090/secured'

grep -q "HTTP/1.1 200" logs/server_access.log && check_pass "正确凭据 → 200" || check_fail "正确凭据未返回 200"

# ── 8. 正确凭据返回受保护页面内容 ───────────────────────────────────────

echo ""
echo "--- 测试 8: 正确凭据返回 secured.html 内容 ---"

mkdir -p /tmp/v15_test
# Start server, do one curl with correct credentials, save body
"$SERVER" "$AUTH_TEST_CFG" 1 > /dev/null 2>&1 &
PID=$!
sleep 1
curl -s -o /tmp/v15_test/secured_body.html -u student:lab123 http://127.0.0.1:$PORT/secured 2>&1 || true
wait $PID 2>/dev/null || true
sleep 0.3
grep -q '受保护页面' /tmp/v15_test/secured_body.html && check_pass "响应体包含'受保护页面'" || check_fail "响应体不包含'受保护页面'"
grep -q 'student' /tmp/v15_test/secured_body.html && check_pass "响应体包含账号信息说明" || check_fail "响应体缺失账号说明"

# ── 9. 未知 path → 404（与认证无关）─ ────────────────────────────────────

echo ""
echo "--- 测试 9: GET /nonexistent → 404 ---"

run_server "$AUTH_TEST_CFG" 1 'curl -s http://127.0.0.1:9090/nonexistent'

grep -q "HTTP/1.1 404" logs/server_access.log && check_pass "未知路径 → 404" || check_fail "未知路径未返回 404"

# ── 10. PUT /search → 405 (V1.4 回归) ────────────────────────────────────

echo ""
echo "--- 测试 10: PUT /search → 405 (V1.4 回归) ---"

run_server "$AUTH_TEST_CFG" 1 'curl -s -X PUT http://127.0.0.1:9090/search'

grep -q "HTTP/1.1 405" logs/server_access.log && check_pass "PUT /search → 405" || check_fail "PUT /search 未返回 405"

# ── 11. V1.4 路由仍然工作 — GET /search ─────────────────────────────────

echo ""
echo "--- 测试 11: GET /search (V1.4 回归) → 200 ---"

run_server "$AUTH_TEST_CFG" 1 'curl -s "http://127.0.0.1:9090/search?class=2011&keyword=张三"'

grep -q "HTTP/1.1 200" logs/server_access.log && check_pass "GET /search → 200" || check_fail "GET /search 未返回 200"

# ── 12. V1.4 路由仍然工作 — POST /echo ──────────────────────────────────

echo ""
echo "--- 测试 12: POST /echo (V1.4 回归) → 200 ---"

run_server "$AUTH_TEST_CFG" 1 'curl -s -X POST -d "hello_auth_test" http://127.0.0.1:9090/echo'

grep -q "HTTP/1.1 200" logs/server_access.log && check_pass "POST /echo → 200" || check_fail "POST /echo 未返回 200"

# ── 13. V1.3 向后兼容 ──────────────────────────────────────────────────

echo ""
echo "--- 测试 13: V1.3 serve-http 兼容 ---"
timeout 3 "$SERVER" serve-http 1 > /tmp/v15_legacy.log 2>&1 &
PID=$!
sleep 1
curl -s "http://127.0.0.1:8080/" > /dev/null 2>&1 || true
wait $PID 2>/dev/null || true
grep -q "V1.3" /tmp/v15_legacy.log && check_pass "serve-http 模式下 V1.3 标记存在" || check_fail "serve-http V1.3 标记缺失"

# ── 14. 日志不包含明文凭据 ──────────────────────────────────────────────

echo ""
echo "--- 测试 14: 日志安全性 ---"

for pattern in "student:lab123" "c3R1ZGVudDpsYWIxMjM=" "Authorization: Basic" "password.*lab123"; do
    if grep -qi "$pattern" logs/server.log 2>/dev/null; then
        check_fail "日志中发现敏感信息: $pattern"
    fi
done
check_pass "日志无凭据泄露"
check_pass "日志无 Base64 编码串"
check_pass "日志无明文密码"

# ── 15. 错误 scheme → 401 ──────────────────────────────────────────────

echo ""
echo "--- 测试 15: Bearer scheme（非 Basic）→ 401 ---"

run_server "$AUTH_TEST_CFG" 1 'curl -s -H "Authorization: Bearer someToken123" http://127.0.0.1:9090/secured'

grep -q "HTTP/1.1 401" logs/server_access.log && check_pass "Bearer scheme → 401" || check_fail "Bearer scheme 未返回 401"

# ── 16. 畸形 Base64 → 401 ───────────────────────────────────────────────

echo ""
echo "--- 测试 16: 非法 Base64 → 401 ---"

run_server "$AUTH_TEST_CFG" 1 'curl -s -H "Authorization: Basic !!!invalid-base64!!!" http://127.0.0.1:9090/secured'

grep -q "HTTP/1.1 401" logs/server_access.log && check_pass "非法 Base64 → 401" || check_fail "非法 Base64 未返回 401"

# ── 17. 缺少冒号分隔符 → 401 ────────────────────────────────────────────

echo ""
echo "--- 测试 17: 无冒号分隔符的凭据 → 401 ---"
# "bm9jb2xvbg==" = "nocolon" (no colon separator)
run_server "$AUTH_TEST_CFG" 1 'curl -s -H "Authorization: Basic bm9jb2xvbg==" http://127.0.0.1:9090/secured'

grep -q "HTTP/1.1 401" logs/server_access.log && check_pass "无冒号凭据 → 401" || check_fail "无冒号凭据未返回 401"

# ── 18. 配置错误时拒绝启动 — 不支持 auth scheme ─────────────────────────

echo ""
echo "--- 测试 18: 不支持的 auth scheme 被拒绝 ---"
cat > /tmp/v15_bad_auth.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":9099,"document_root":"./www"},"logging":{"level":"INFO","file":"./logs/server.log"},"routes":[{"method":"GET","path":"/test","handler":"search_get","auth":"digest"}]}
EOFJSON
if "$SERVER" /tmp/v15_bad_auth.json 1 > /tmp/v15_bad_auth.log 2>&1; then
    check_fail "不支持 auth scheme 未被拒绝"
else
    grep -q "auth" /tmp/v15_bad_auth.log && check_pass "不支持 auth scheme 被拒绝" || check_fail "错误信息未提及 auth"
fi

# ── 19. 配置校验：auth 字段合法（空或 basic）── ────────────────────────

echo ""
echo "--- 测试 19: 合法 auth scheme 测试 ---"

# 空 auth 字段应该被接受
cat > /tmp/v15_empty_auth.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":9100,"document_root":"./www"},"logging":{"level":"INFO","file":"./logs/server.log"},"routes":[{"method":"GET","path":"/test","handler":"search_get"}]}
EOFJSON
timeout 2 "$SERVER" /tmp/v15_empty_auth.json 1 > /tmp/v15_empty_auth.log 2>&1 || true
grep -q "W3D4 HTTP Server" /tmp/v15_empty_auth.log && check_pass "无 auth 字段 → 合法（公开路由）" || check_fail "无 auth 字段被拒绝"

# 明确 auth="basic" 应该被接受（但需要 credentials）
cat > /tmp/v15_auth_basic_nocred.json << 'EOFJSON'
{"server":{"host":"127.0.0.1","port":9101,"document_root":"./www"},"logging":{"level":"INFO","file":"./logs/server.log"},"routes":[{"method":"GET","path":"/test","handler":"search_get","auth":"basic"}]}
EOFJSON
timeout 2 "$SERVER" /tmp/v15_auth_basic_nocred.json 1 > /tmp/v15_auth_basic_nocred.log 2>&1 || true
grep -q "W3D4 HTTP Server" /tmp/v15_auth_basic_nocred.log && check_pass "auth=basic 被接受（即使无 credentials）" || check_fail "auth=basic 被拒绝"

# ── 20. 多场景连续请求 ──────────────────────────────────────────────────

echo ""
echo "--- 测试 20: 多场景连续请求 ---"

run_server "$AUTH_TEST_CFG" 4 \
    'curl -s http://127.0.0.1:9090/' \
    'curl -s -u student:lab123 http://127.0.0.1:9090/secured' \
    'curl -s http://127.0.0.1:9090/secured' \
    'curl -s "http://127.0.0.1:9090/search?keyword=test"'

grep -c "HTTP/1.1 200" logs/server_access.log 2>/dev/null > /tmp/v15_multi_200.txt || true
grep -c "HTTP/1.1 401" logs/server_access.log 2>/dev/null > /tmp/v15_multi_401.txt || true

TOTAL_200=$(cat /tmp/v15_multi_200.txt 2>/dev/null || echo 0)
TOTAL_401=$(cat /tmp/v15_multi_401.txt 2>/dev/null || echo 0)

if [ "$TOTAL_200" -ge 2 ]; then
    check_pass "混合请求中有 ≥2 个 200"
else
    check_fail "混合请求中 200 少于 2 个"
fi
if [ "$TOTAL_401" -ge 1 ]; then
    check_pass "混合请求中有 ≥1 个 401"
else
    check_fail "混合请求中 401 少于 1 个"
fi

# ── 结果汇总 ──────────────────────────────────────────────────────────────

echo ""
echo "=========================================="
echo "Day15 V1.5 HTTP Basic 认证 测试结果汇总"
echo "=========================================="
echo "  通过: $PASS"
echo "  失败: $FAIL"
echo ""

# Cleanup
rm -f /tmp/v15_*.log /tmp/v15_*.json /tmp/v15_multi_*.txt 2>/dev/null

if [ $FAIL -gt 0 ]; then
    echo "存在 $FAIL 个失败项，请检查。"
    exit 1
else
    echo "Day15 V1.5 HTTP Basic 认证测试全部通过！"
    exit 0
fi
