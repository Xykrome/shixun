# Task1: 简单 Shell 程序 Makefile
CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = simple_shell
SRC = simple_shell.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

run: $(TARGET)
	@echo "启动简单 Shell 程序..."
	@echo "可以输入 ls, pwd, whoami 等命令测试"
	@echo "输入 exit 退出"
	@echo ""
	./$(TARGET)
