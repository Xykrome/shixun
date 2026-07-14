# W2D1 训练1：基于TCP的文本聊天程序
# Makefile

CC      = gcc
CFLAGS  = -Wall -Wextra -g
LDFLAGS =           # TCP套接字编程无需 -lpthread（socket API 在标准C库中）

# 目标
SERVER = server
CLIENT = client

.PHONY: all clean run-server run-client

all: $(SERVER) $(CLIENT)

$(SERVER): server.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(CLIENT): client.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# 运行服务器（默认端口 8080）
run-server: $(SERVER)
	@echo "=========================================="
	@echo "  启动 TCP 聊天服务器，监听端口 8080"
	@echo "  等待客户端连接..."
	@echo "=========================================="
	./$(SERVER)

# 运行客户端（连接 127.0.0.1:8080）
run-client: $(CLIENT)
	@echo "=========================================="
	@echo "  启动 TCP 聊天客户端"
	@echo "  连接 127.0.0.1:8080"
	@echo "=========================================="
	./$(CLIENT) 127.0.0.1 8080

clean:
	rm -f $(SERVER) $(CLIENT)
