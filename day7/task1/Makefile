# W2D2 训练1：多客户TCP文本聊天程序 Makefile
#
# 目标：
#   make          — 编译 server 和 client
#   make server   — 仅编译服务器
#   make client   — 仅编译客户端
#   make clean    — 清理编译产物
#   make test     — 快速验证编译结果

CC       = gcc
CFLAGS   = -Wall -Wextra -g -O2
LDFLAGS  =

# 源文件
SERVER_SRC = server.c
CLIENT_SRC = client.c

# 可执行文件
SERVER_BIN = server
CLIENT_BIN = client

.PHONY: all clean test build-server build-client

all: $(SERVER_BIN) $(CLIENT_BIN)

# ===== 编译服务器 =====
$(SERVER_BIN): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "[make] 服务器编译完成 → $(SERVER_BIN)"

# ===== 编译客户端 =====
$(CLIENT_BIN): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "[make] 客户端编译完成 → $(CLIENT_BIN)"

# ===== 便捷别名 =====
build-server: $(SERVER_BIN)
build-client: $(CLIENT_BIN)

# ===== 清理 =====
clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	@echo "[make] 已清理"

# ===== 快速验证 =====
test: all
	@echo ""
	@echo "=========================================="
	@echo " W2D2 训练1 编译验证"
	@echo "=========================================="
	@echo ""
	@if [ -x "$(SERVER_BIN)" ]; then \
		echo "[PASS] server 编译成功"; \
	else \
		echo "[FAIL] server 编译失败"; \
	fi
	@if [ -x "$(CLIENT_BIN)" ]; then \
		echo "[PASS] client 编译成功"; \
	else \
		echo "[FAIL] client 编译失败"; \
	fi
	@echo ""
	@echo "--- 使用说明 ---"
	@echo "  1. 先在一个终端启动服务器:"
	@echo "     ./server"
	@echo ""
	@echo "  2. 在另一个终端启动客户端（可启动多个）:"
	@echo "     ./client 127.0.0.1 8080"
	@echo ""
	@echo "  3. 在客户端输入消息，服务器会回射消息"
	@echo "     输入 'exit' 退出"
	@echo ""
	@echo "  4. 同时启动多个客户端，验证多进程并发:"
	@echo "     终端1: ./client 127.0.0.1 8080"
	@echo "     终端2: ./client 127.0.0.1 8080"
	@echo "     终端3: ./client 127.0.0.1 8080"
	@echo ""
	@echo "  5. 查看服务器日志，验证子进程创建和回收:"
	@echo "     服务器会打印 [子进程 PID] 和 [服务器] 子进程 PID 已终止"
