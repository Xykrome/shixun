#!/bin/bash
# test_day03.sh - 验证 V0.3 索引功能

EXE="./mini_web_server"
DATA_DIR="data"
USER_CSV="$DATA_DIR/users.csv"

# 准备测试数据（包含一些用户）
cat > $USER_CSV <<EOF
username,password,phone
zhangsan,123,111
lisi,456,222
wangwu,789,333
zhaoliu,abc,444
EOF

echo "=== 1. 构建索引并中序遍历（按用户名升序） ==="
$EXE index | grep -q "lisi" && echo "PASS: 中序遍历包含 lisi" || echo "FAIL: 未找到 lisi"
$EXE index | grep -q "wangwu" && echo "PASS: 中序遍历包含 wangwu" || echo "FAIL: 未找到 wangwu"

echo "=== 2. find-index 查找存在的用户 ==="
$EXE find-index zhangsan | grep -q "FOUND" && echo "PASS: find-index zhangsan 成功" || echo "FAIL"
$EXE find-index lisi | grep -q "FOUND" && echo "PASS: find-index lisi 成功" || echo "FAIL"

echo "=== 3. find-index 查找不存在的用户 ==="
$EXE find-index nobody | grep -q "NOT_FOUND" && echo "PASS: find-index nobody 输出 NOT_FOUND" || echo "FAIL"

echo "=== 4. compare 输出链表和索引的查找步骤 ==="
$EXE compare wangwu > compare_output.txt
grep -q "链表查找过程" compare_output.txt && echo "PASS: compare 输出链表查找过程" || echo "FAIL"
grep -q "索引查找过程" compare_output.txt && echo "PASS: compare 输出索引查找过程" || echo "FAIL"

echo "=== 5. 释放索引树后程序正常退出 ==="
# 通过 valgrind 检测内存泄漏（可选），但至少确保没有段错误
$EXE index > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "PASS: 程序正常退出"
else
    echo "FAIL: 程序异常退出"
fi

rm -f compare_output.txt
echo "测试完成。"