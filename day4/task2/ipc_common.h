/*
 * Task2: 生产者-消费者问题 - 公共头文件
 *
 * 定义共享内存结构体、信号量/共享内存键值、信号量操作函数
 */

#ifndef IPC_COMMON_H
#define IPC_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>

/* IPC 键值 */
#define SHM_KEY         0x1234      /* 共享内存键值 */
#define SEM_KEY         0x1a0a      /* 信号量集键值 */

/* 缓冲池配置 */
#define BUFFER_COUNT    5           /* 缓冲区数量 */
#define BUFFER_SIZE     100         /* 每个缓冲区大小(字节) */
#define PRODUCER_COUNT  2           /* 生产者数量 */
#define CONSUMER_COUNT  2           /* 消费者数量 */
#define MAX_LOOP        10          /* 每个生产/消费者的最大循环次数 */

/* 缓冲池结构体 */
struct BufferPool {
    char buffer[BUFFER_COUNT][BUFFER_SIZE];  /* 5个缓冲区 */
    int  index[BUFFER_COUNT];                /* 缓冲区状态:
                                                0 = 未被生产者使用(可分配)
                                                1 = 已被生产者使用(可消费) */
};

/* 信号量集操作编号 */
#define SEM_MUTEX       0           /* 互斥信号量编号 */

/* ===== semun 联合体（部分系统需手动定义）===== */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

/* ===== 信号量操作函数 ===== */

/* 初始化信号量 */
static inline int init_sem(int sem_id, int init_value)
{
    union semun sem_union;
    sem_union.val = init_value;
    if (semctl(sem_id, 0, SETVAL, sem_union) == -1) {
        perror("Initialize semaphore");
        return -1;
    }
    return 1;
}

/* P 操作（分配资源） */
static inline int semaphore_p(int sem_id, short sem_no)
{
    struct sembuf sem_b;
    sem_b.sem_num = sem_no;    /* 信号量集中信号量编号 */
    sem_b.sem_op  = -1;        /* P操作，每次分配1个资源 */
    sem_b.sem_flg = SEM_UNDO;
    if (semop(sem_id, &sem_b, 1) == -1) {
        perror("semaphore_p failed");
        return 0;
    }
    return 1;
}

/* V 操作（释放资源） */
static inline int semaphore_v(int sem_id, short sem_no)
{
    struct sembuf sem_b;
    sem_b.sem_num = sem_no;    /* 信号量集中信号量编号 */
    sem_b.sem_op  = 1;         /* V操作，每次释放1个资源 */
    sem_b.sem_flg = SEM_UNDO;
    if (semop(sem_id, &sem_b, 1) == -1) {
        perror("semaphore_v failed");
        return 0;
    }
    return 1;
}

/* 删除信号量对象 */
static inline int del_sem(int sem_id)
{
    union semun sem_union;
    if (semctl(sem_id, 0, IPC_RMID, sem_union) == -1) {
        perror("Delete semaphore");
        return -1;
    }
    return 1;
}

#endif /* IPC_COMMON_H */
