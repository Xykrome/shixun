/*
 * Task2: 生产者程序
 *
 * 功能：
 * - 从 producerN.txt 文件中读取数据
 * - 通过 XSI 共享内存访问共享缓冲池
 * - 通过 XSI 信号量集实现对缓冲池的互斥访问
 * - 将数据写入未被使用的缓冲区（状态为0的缓冲区）
 * - 写入后将缓冲区状态设置为1
 *
 * 用法：./producer <生产者编号>
 * 示例：./producer 1
 *       从 producer1.txt 读取内容写入缓冲池
 */

#include "ipc_common.h"

int main(int argc, char *argv[])
{
    int producer_id;                    /* 生产者编号 */
    char filename[64];                  /* 输入文件名 */
    FILE *fp;
    int shmid;                          /* 共享内存ID */
    int semid;                          /* 信号量集ID */
    struct BufferPool *pool;            /* 缓冲池指针 */
    void *shm = NULL;
    int running = 1;
    int loop_count = 0;

    /* 解析参数 */
    if (argc < 2) {
        fprintf(stderr, "用法: %s <生产者编号>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    producer_id = atoi(argv[1]);
    snprintf(filename, sizeof(filename), "producer%d.txt", producer_id);

    printf("[生产者 %d] 启动, PID=%d, 读取文件: %s\n",
           producer_id, getpid(), filename);

    /* 打开输入文件 */
    fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "[生产者 %d] 无法打开文件: %s\n", producer_id, filename);
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    /* 获取/创建共享内存 */
    shmid = shmget((key_t)SHM_KEY, sizeof(struct BufferPool), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget 失败");
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    /* 连接共享内存 */
    shm = shmat(shmid, (void *)0, 0);
    if (shm == (void *)-1) {
        perror("shmat 失败");
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    pool = (struct BufferPool *)shm;
    printf("[生产者 %d] 共享内存连接成功, 地址=%p\n", producer_id, (void *)pool);

    /* 获取信号量集 */
    semid = semget((key_t)SEM_KEY, 1, 0666 | IPC_CREAT);
    if (semid == -1) {
        perror("semget 失败");
        shmdt(shm);
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    printf("[生产者 %d] 信号量集获取成功, semid=%d\n", producer_id, semid);

    /* 生产者主循环 */
    while (running && loop_count < MAX_LOOP) {
        char line[BUFFER_SIZE];
        int found = 0;

        /* 从文件读取一行 */
        if (fgets(line, sizeof(line), fp) == NULL) {
            /* 文件读完，从头开始 */
            rewind(fp);
            loop_count++;
            if (fgets(line, sizeof(line), fp) == NULL) {
                break;
            }
        }

        /* 去除换行符 */
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) {
            continue;
        }

        /* P 操作：申请访问缓冲池的互斥权 */
        if (!semaphore_p(semid, SEM_MUTEX)) {
            running = 0;
            break;
        }

        /* 临界区：查找未被生产者使用的缓冲区 */
        for (int i = 0; i < BUFFER_COUNT; i++) {
            if (pool->index[i] == 0) {
                /* 找到空闲缓冲区，写入数据 */
                strncpy(pool->buffer[i], line, BUFFER_SIZE - 1);
                pool->buffer[i][BUFFER_SIZE - 1] = '\0';
                pool->index[i] = 1;  /* 标记为已被生产者使用 */
                found = 1;
                printf("[生产者 %d] 写入缓冲区[%d]: \"%s\"\n",
                       producer_id, i, line);
                break;
            }
        }

        if (!found) {
            printf("[生产者 %d] 缓冲池已满，等待消费者消费...\n", producer_id);
        }

        /* V 操作：释放互斥权 */
        semaphore_v(semid, SEM_MUTEX);

        /* 随机睡眠，模拟生产时间 */
        usleep((rand() % 500 + 100) * 1000);  /* 100ms~600ms */
    }

    printf("[生产者 %d] 结束，共循环 %d 次\n", producer_id, loop_count);

    /* 清理 */
    shmdt(shm);
    fclose(fp);
    return 0;
}
