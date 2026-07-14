# Task2: 生产者-消费者 Makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -g
LDFLAGS =

TARGETS = producer consumer launcher

.PHONY: all clean run

all: $(TARGETS)

producer: producer.c ipc_common.h
	$(CC) $(CFLAGS) -o $@ $<

consumer: consumer.c ipc_common.h
	$(CC) $(CFLAGS) -o $@ $<

launcher: launcher.c ipc_common.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
	rm -f consumer*.txt

run: all
	@echo "========================================="
	@echo "  启动多进程生产者-消费者演示"
	@echo "========================================="
	./launcher
	@echo ""
	@echo "--- 消费者输出文件 ---"
	@for f in consumer*.txt; do \
		if [ -f "$$f" ]; then \
			echo "  $$f:"; \
			cat "$$f"; \
		fi; \
	done

# 单独运行生产者（每个终端运行一个）
run-producer1: producer
	./producer 1

run-producer2: producer
	./producer 2

# 单独运行消费者（每个终端运行一个）
run-consumer1: consumer
	./consumer 1

run-consumer2: consumer
	./consumer 2
