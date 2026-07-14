/*
 * Task2: 多进程启动器
 *
 * 功能：
 * - 创建 XSI 共享内存和 XSI 信号量集
 * - 初始化信号量和缓冲池
 * - 通过 fork + exec 创建多个生产者子进程和消费者子进程
 * - 使用 waitpid 等待所有子进程结束
 * - 清理 IPC 资源
 *
 * 用法：./launcher
 */

#include "ipc_common.h"

int main(void)
{
    int shmid;                                  /* 共享内存ID */
    int semid;                                  /* 信号量集ID */
    struct BufferPool *pool;                    /* 缓冲池指针 */
    void *shm = NULL;
    pid_t child_pids[PRODUCER_COUNT + CONSUMER_COUNT];  /* 所有子进程PID */
    int child_count = 0;

    printf("========================================\n");
    printf("  生产者-消费者 多进程演示 (Task2)\n");
    printf("========================================\n\n");

    /* ===== 1. 创建共享内存 ===== */
    shmid = shmget((key_t)SHM_KEY, sizeof(struct BufferPool), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget 失败");
        exit(EXIT_FAILURE);
    }
    printf("[启动器] 共享内存创建成功, shmid=%d\n", shmid);

    /* 连接共享内存 */
    shm = shmat(shmid, (void *)0, 0);
    if (shm == (void *)-1) {
        perror("shmat 失败");
        exit(EXIT_FAILURE);
    }
    pool = (struct BufferPool *)shm;

    /* 初始化缓冲池：所有缓冲区状态置为0（可分配） */
    for (int i = 0; i < BUFFER_COUNT; i++) {
        pool->index[i] = 0;
        memset(pool->buffer[i], 0, BUFFER_SIZE);
    }
    printf("[启动器] 缓冲池初始化完成 (%d个缓冲区, 每个%d字节)\n",
           BUFFER_COUNT, BUFFER_SIZE);

    /* 断开共享内存（之后子进程各自连接） */
    shmdt(shm);

    /* ===== 2. 创建信号量集 ===== */
    semid = semget((key_t)SEM_KEY, 1, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("semget 失败");
        shmctl(shmid, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    /* 初始化互斥信号量为1 */
    if (init_sem(semid, 1) == -1) {
        shmctl(shmid, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }
    printf("[启动器] 信号量集创建成功, semid=%d, 互斥信号量初始值=1\n\n", semid);

    /* ===== 3. 创建生产者子进程 ===== */
    printf("--- 创建 %d 个生产者进程 ---\n", PRODUCER_COUNT);
    for (int i = 1; i <= PRODUCER_COUNT; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork 失败");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            /* 子进程：执行生产者程序 */
            char producer_id[8];
            snprintf(producer_id, sizeof(producer_id), "%d", i);

            printf("[启动器] 创建生产者子进程 PID=%d, 编号=%d\n", getpid(), i);
            execl("./producer", "./producer", producer_id, NULL);

            /* execl成功不返回，执行到这里说明失败 */
            perror("execl 生产者失败");
            exit(EXIT_FAILURE);
        } else {
            /* 父进程记录子进程PID */
            child_pids[child_count++] = pid;
            printf("[启动器] 生产者子进程 PID=%d 已创建\n", pid);
        }
    }
    printf("\n");

    /* ===== 4. 创建消费者子进程 ===== */
    printf("--- 创建 %d 个消费者进程 ---\n", CONSUMER_COUNT);
    for (int i = 1; i <= CONSUMER_COUNT; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork 失败");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            /* 子进程：执行消费者程序 */
            char consumer_id[8];
            snprintf(consumer_id, sizeof(consumer_id), "%d", i);

            printf("[启动器] 创建消费者子进程 PID=%d, 编号=%d\n", getpid(), i);
            execl("./consumer", "./consumer", consumer_id, NULL);

            /* execl成功不返回，执行到这里说明失败 */
            perror("execl 消费者失败");
            exit(EXIT_FAILURE);
        } else {
            /* 父进程记录子进程PID */
            child_pids[child_count++] = pid;
            printf("[启动器] 消费者子进程 PID=%d 已创建\n", pid);
        }
    }
    printf("\n");

    /* ===== 5. 等待所有子进程结束 ===== */
    printf("--- 等待所有子进程结束 ---\n");
    for (int i = 0; i < child_count; i++) {
        int status;
        pid_t ret_pid;

        printf("[启动器] 等待子进程 PID=%d 结束...\n", child_pids[i]);

        /* 使用 waitpid 等待特定子进程终止 */
        ret_pid = waitpid(child_pids[i], &status, 0);

        if (ret_pid == -1) {
            perror("waitpid 失败");
        } else if (WIFEXITED(status)) {
            printf("[启动器] 子进程 PID=%d 正常结束, 退出码=%d\n",
                   ret_pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[启动器] 子进程 PID=%d 被信号 %d 终止\n",
                   ret_pid, WTERMSIG(status));
        }
    }
    printf("\n");

    /* ===== 6. 查看缓冲池最终状态 ===== */
    shm = shmat(shmid, (void *)0, 0);
    if (shm != (void *)-1) {
        pool = (struct BufferPool *)shm;
        printf("--- 缓冲池最终状态 ---\n");
        for (int i = 0; i < BUFFER_COUNT; i++) {
            printf("  缓冲区[%d]: 状态=%d, 内容=\"%s\"\n",
                   i, pool->index[i], pool->buffer[i]);
        }
        shmdt(shm);
    }

    /* ===== 7. 清理IPC资源 ===== */
    printf("\n--- 清理IPC资源 ---\n");

    /* 删除信号量集 */
    if (del_sem(semid) == 1) {
        printf("[启动器] 信号量集 (semid=%d) 已删除\n", semid);
    }

    /* 删除共享内存 */
    if (shmctl(shmid, IPC_RMID, NULL) == 0) {
        printf("[启动器] 共享内存 (shmid=%d) 已删除\n", shmid);
    }

    printf("\n[启动器] 所有任务完成！\n");
    return 0;
}
