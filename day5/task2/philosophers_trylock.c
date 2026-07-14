/**
 * philosophers_trylock.c — 哲学家进餐问题（非阻塞加锁版本）
 *
 * 使用 pthread_mutex_trylock 非阻塞方式拿起筷子。
 * 当哲学家无法同时拿到两根筷子时，会释放已持有的筷子并等待，
 * 通过"让权等待"思想预防死锁。
 *
 * 哲学家数量通过 N 宏定义，默认为 5。
 * 编译：gcc philosophers_trylock.c -o philosophers_trylock -lpthread
 * 运行：./philosophers_trylock
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

/* ===== 可配置参数 ===== */
#define N          5      /* 哲学家数量（同时也是筷子数量） */
#define MAX_ROUND  30     /* 每个哲学家最多进餐次数 */

/* ===== 全局共享资源 ===== */
pthread_mutex_t chopstick[N];  /* N 根筷子互斥量 */
int eat_count[N];              /* 每个哲学家的进餐次数 */
int total_retry;               /* 总重试次数（统计用） */

/**
 * try_takechopstick — 非阻塞方式尝试拿起两根筷子
 *   1. 先 trylock 左边筷子
 *   2. 成功后再 trylock 右边筷子
 *   3. 若右边筷子被占用，则释放已持有的左边筷子（让权等待）
 *   返回值：1 = 成功拿到两根筷子，0 = 失败（需重试）
 */
int try_takechopstick(int i)
{
    int left  = i;
    int right = (i + 1) % N;

    /* 第一步：尝试拿左边筷子 */
    if (pthread_mutex_trylock(&chopstick[left]) != 0) {
        /* 左边筷子被占用，直接返回失败 */
        return 0;
    }

    /* 第二步：已持有左边，尝试拿右边筷子 */
    if (pthread_mutex_trylock(&chopstick[right]) != 0) {
        /* 右边筷子被占用，释放左边筷子，让其他哲学家有机会使用 */
        pthread_mutex_unlock(&chopstick[left]);
        return 0;
    }

    /* 成功拿到两根筷子 */
    return 1;
}

/**
 * putchopstick — 放下两根筷子
 */
void putchopstick(int i)
{
    pthread_mutex_unlock(&chopstick[i]);
    pthread_mutex_unlock(&chopstick[(i + 1) % N]);
}

/**
 * philosopher — 哲学家线程启动例程
 *   循环执行：思考 → 尝试拿筷子 → 失败则等待重试 → 进餐 → 放筷子
 *   "让权等待"：拿不到筷子时不阻塞等待，而是主动释放已有资源并 sleep 后重试
 */
void *philosopher(void *arg)
{
    int i = *(int *)arg;
    int retry = 0;

    while (eat_count[i] < MAX_ROUND) {
        /* 思考 */
        printf("Philosopher %d is thinking.\n", i);
        usleep((rand() % 500 + 100) * 1000);

        /* 尝试拿筷子（非阻塞，失败则让权等待） */
        retry = 0;
        while (!try_takechopstick(i)) {
            retry++;
            /* 让权等待：释放 CPU，随机延时后重试，避免活锁 */
            usleep((rand() % 200 + 50) * 1000);
        }
        if (retry > 0) {
            printf("Philosopher %d retried %d times to get chopsticks.\n", i, retry);
        }

        /* 进餐 */
        eat_count[i]++;
        printf("Philosopher %d is eating  [round %d].\n", i, eat_count[i]);
        usleep((rand() % 300 + 100) * 1000);

        /* 放下筷子 */
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
    printf("Mode: NON-BLOCKING lock (pthread_mutex_trylock)\n");
    printf("Strategy: yield-and-wait to prevent deadlock\n");
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

    printf("\nAll philosophers finished without deadlock.\n");

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
