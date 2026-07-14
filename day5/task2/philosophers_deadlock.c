/**
 * philosophers_deadlock.c — 哲学家进餐问题（阻塞加锁版本）
 *
 * 使用 pthread_mutex_lock 阻塞方式拿起筷子。
 * 每个哲学家总是先拿左边筷子，再拿右边筷子。
 * 当所有哲学家同时拿起左边筷子时，程序将进入死锁状态。
 *
 * 哲学家数量通过 N 宏定义，默认为 5。
 * 编译：gcc philosophers_deadlock.c -o philosophers_deadlock -lpthread
 * 运行：./philosophers_deadlock
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

/* ===== 可配置参数 ===== */
#define N          5      /* 哲学家数量（同时也是筷子数量） */
#define MAX_ROUND  30     /* 每个哲学家最多进餐次数，防止无限循环 */

/* ===== 全局共享资源 ===== */
pthread_mutex_t chopstick[N];  /* N 根筷子，每个是一把互斥锁 */
int eat_count[N];              /* 记录每个哲学家的进餐次数 */

/**
 * takechopstick — 阻塞方式拿起筷子
 *   先锁定左边的筷子 i，再锁定右边的筷子 (i+1)%N
 *   如果所有哲学家同时拿起了左边筷子，所有人都会阻塞在拿右边筷子上 → 死锁
 *   同时，在拿左筷子和右筷子之间加入短暂延时，
 *   增大所有哲学家同时持有左筷子等待右筷子的概率，稳定复现死锁。
 */
void takechopstick(int i)
{
    pthread_mutex_lock(&chopstick[i]);               /* 拿左边筷子 */
    usleep(500 * 1000);                              /* 延时500ms，确保所有哲学家都能拿到左边 */
    pthread_mutex_lock(&chopstick[(i + 1) % N]);     /* 拿右边筷子 */
}

/**
 * putchopstick — 放下筷子
 */
void putchopstick(int i)
{
    pthread_mutex_unlock(&chopstick[i]);
    pthread_mutex_unlock(&chopstick[(i + 1) % N]);
}

/**
 * philosopher — 哲学家线程启动例程
 *   循环执行：思考 → 拿筷子 → 进餐 → 放筷子
 */
void *philosopher(void *arg)
{
    int i = *(int *)arg;

    while (eat_count[i] < MAX_ROUND) {
        /* 思考 */
        printf("Philosopher %d is thinking.\n", i);
        sleep(1);                               /* 固定思考1秒，使哲学家同步 */

        /* 拿筷子（阻塞方式，可能在此处死锁） */
        printf("Philosopher %d is going to take chopsticks.\n", i);
        takechopstick(i);

        /* 进餐 */
        eat_count[i]++;
        printf("Philosopher %d is eating  [round %d].\n", i, eat_count[i]);
        sleep(1);                               /* 固定进餐1秒 */

        /* 放筷子 */
        putchopstick(i);
        printf("Philosopher %d put down chopsticks.\n", i);
    }

    printf("Philosopher %d has finished all %d rounds.\n", i, MAX_ROUND);
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t tid[N];
    int id[N];
    int i;

    (void)argc; (void)argv;  /* suppress unused warning */
    srand(time(NULL));

    printf("===== Philosophers Dining Problem =====\n");
    printf("Philosophers: %d | Max rounds: %d\n", N, MAX_ROUND);
    printf("Mode: BLOCKING lock (pthread_mutex_lock)\n");
    printf("WARNING: Deadlock will occur when all philosophers\n");
    printf("         pick up left chopsticks simultaneously!\n");
    printf("========================================\n\n");

    /* 初始化 N 根筷子（互斥量） */
    for (i = 0; i < N; i++) {
        pthread_mutex_init(&chopstick[i], NULL);
        eat_count[i] = 0;
    }

    /* 创建 N 个哲学家线程 */
    for (i = 0; i < N; i++) {
        id[i] = i;
        pthread_create(&tid[i], NULL, philosopher, (void *)&id[i]);
    }

    /* 主线程等待所有哲学家线程结束 */
    for (i = 0; i < N; i++) {
        pthread_join(tid[i], NULL);
    }

    printf("\nAll philosophers finished.\n");

    /* 统计进餐情况 */
    for (i = 0; i < N; i++) {
        printf("Philosopher %d ate %d times.\n", i, eat_count[i]);
    }

    /* 销毁互斥量 */
    for (i = 0; i < N; i++) {
        pthread_mutex_destroy(&chopstick[i]);
    }

    return 0;
}
