#!/bin/bash
# test_day02.sh - 验证用户管理功能

echo "=== Test list (initial) ==="
./mini_web_server list

echo "=== Test find zhangsan ==="
./mini_web_server find zhangsan

echo "=== Test find nobody ==="
./mini_web_server find nobody

echo "=== Test add newuser ==="
./mini_web_server add newuser pass123 13811111111

echo "=== Test list after add ==="
./mini_web_server list

echo "=== Test add duplicate zhangsan ==="
./mini_web_server add zhangsan 123456 13800000000

echo "=== Test delete zhangsan ==="
./mini_web_server delete zhangsan

echo "=== Test list after delete ==="
./mini_web_server list

echo "=== Test find zhangsan after delete ==="
./mini_web_server find zhangsan