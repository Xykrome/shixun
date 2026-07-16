CC      = gcc
CFLAGS  = -Wall -Iinclude
LDFLAGS =

# 源文件
SERVER_SRCS = src/main.c src/epoll_server.c
CLIENT_SRCS = src/chat_client.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# 目标文件
SERVER_TARGET = epoll_server
CLIENT_TARGET = chat_client

.PHONY: all clean server client

all: server client

server: $(SERVER_TARGET)

client: $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(SERVER_TARGET) $(CLIENT_TARGET)

# 快捷运行
run-server: server
	./$(SERVER_TARGET) 8888

run-client: client
	./$(CLIENT_TARGET) 127.0.0.1 8888

# Day10 测试
test: all
	./tests/test_day10.sh
