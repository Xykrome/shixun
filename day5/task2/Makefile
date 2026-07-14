# Day 5 Task 2: Philosophers Dining Problem
# Makefile for building both deadlock and trylock versions

CC      = gcc
CFLAGS  = -Wall -Wextra -g
LDFLAGS = -lpthread

# Targets
DEADLOCK = philosophers_deadlock
TRYLOCK  = philosophers_trylock

.PHONY: all clean run_deadlock run_trylock run

all: $(DEADLOCK) $(TRYLOCK)

$(DEADLOCK): philosophers_deadlock.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(TRYLOCK): philosophers_trylock.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# 运行阻塞版本（观察死锁现象，约 10 秒无输出即说明已死锁）
run_deadlock: $(DEADLOCK)
	@echo "========================================"
	@echo "Running BLOCKING version..."
	@echo "Watch for deadlock after a few rounds!"
	@echo "Press Ctrl+C to stop when deadlock occurs."
	@echo "========================================"
	./$(DEADLOCK)

# 运行非阻塞版本（让权等待预防死锁）
run_trylock: $(TRYLOCK)
	@echo "========================================"
	@echo "Running NON-BLOCKING trylock version..."
	@echo "This version should NOT deadlock."
	@echo "========================================"
	./$(TRYLOCK)

# 默认运行非阻塞版本
run: run_trylock

clean:
	rm -f $(DEADLOCK) $(TRYLOCK)
