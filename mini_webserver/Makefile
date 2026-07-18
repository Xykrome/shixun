CC = gcc
CFLAGS = -Wall -Iinclude
LDFLAGS = -lpthread
SRCS = src/main.c src/config.c src/log.c src/http_response.c src/user_store.c src/user_index.c src/process_server.c src/request_handler.c src/thread_server.c src/tcp_server.c src/tcp_fork_server.c src/thread_pool.c src/tcp_pool_server.c src/epoll_server.c src/http_parser.c src/http_server.c
OBJS = $(SRCS:.c=.o)
TARGET = mini_web_server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET) conf/server.conf

# 原有测试（Day01）
test: $(TARGET)
	./tests/test_day01.sh

# Day02 测试
test2: $(TARGET)
	./tests/test_day02.sh

# Day03 测试（索引功能）
test3: $(TARGET)
	./tests/test_day03.sh

# Day04 测试（多进程处理）
test4: $(TARGET)
	./tests/test_day04.sh

# Day05 测试（多线程处理）
test5: $(TARGET)
	./tests/test_day05.sh

# Day06 测试（TCP 网络服务器 — 单连接版）
test6: $(TARGET)
	./tests/test_day06.sh

# Day07 测试（TCP 网络服务器 — 多进程并发版）
test7: $(TARGET)
	./tests/test_day07.sh

# Day08 测试（TCP 网络服务器 — 线程池并发版 V0.8）
test8: $(TARGET)
	./tests/test_day08.sh

# Day10 测试（W2D5 — Epoll Webserver V1.0）
test9: $(TARGET)
	./tests/test_day10.sh

# Day11 测试（W3D1 — HTTP Server V1.1）
test10: $(TARGET)
	./tests/test_day11.sh

# 运行所有测试
test-all: $(TARGET)
	./tests/test_day01.sh
	./tests/test_day02.sh
	./tests/test_day03.sh
	./tests/test_day04.sh
	./tests/test_day05.sh
	./tests/test_day06.sh
	./tests/test_day07.sh
	./tests/test_day08.sh

.PHONY: all clean run test test2 test3 test4 test5 test6 test7 test8 test9 test10 test-all
