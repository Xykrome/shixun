/*
 * Task2: 消费者程序
 *
 * 功能：
 * - 通过 XSI 共享内存访问共享缓冲池
 * - 通过 XSI 信号量集实现对缓冲池的互斥访问
 * - 从已被生产者使用的缓冲区（状态为1的缓冲区）读取数据
 * - 将数据写入 consumerN.txt 文件并打印
 * - 读取后将缓冲区状态设置为0
 *
 * 用法：./consumer <消费者编号>
 * 示例：./consumer 1
 *       从缓冲池读取内容写入 consumer1.txt
 */

#include "ipc_common.h"

int main(int argc, char *argv[])
{
    int consumer_id;                    /* 消费者编号 */
    char filename[64];                  /* 输出文件名 */
    FILE *fp;
    int shmid;                          /* 共享内存ID */
    int semid;                          /* 信号量集ID */
    struct BufferPool *pool;            /* 缓冲池指针 */
    void *shm = NULL;
    int running = 1;
    int loop_count = 0;
    int total_consumed = 0;

    /* 解析参数 */
    if (argc < 2) {
        fprintf(stderr, "用法: %s <消费者编号>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    consumer_id = atoi(argv[1]);
    snprintf(filename, sizeof(filename), "consumer%d.txt", consumer_id);

    printf("[消费者 %d] 启动, PID=%d, 写入文件: %s\n",
           consumer_id, getpid(), filename);

    /* 打开输出文件 */
    fp = fopen(filename, "w");
    if (fp == NULL) {
        fprintf(stderr, "[消费者 %d] 无法打开文件: %s\n", consumer_id, filename);
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    /* 获取共享内存 */
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
    printf("[消费者 %d] 共享内存连接成功, 地址=%p\n", consumer_id, (void *)pool);

    /* 获取信号量集 */
    semid = semget((key_t)SEM_KEY, 1, 0666 | IPC_CREAT);
    if (semid == -1) {
        perror("semget 失败");
        shmdt(shm);
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    printf("[消费者 %d] 信号量集获取成功, semid=%d\n", consumer_id, semid);

    /* 消费者主循环 */
    while (running && loop_count < MAX_LOOP * 2) {
        int found = 0;
        char text[BUFFER_SIZE];

        /* P 操作：申请访问缓冲池的互斥权 */
        if (!semaphore_p(semid, SEM_MUTEX)) {
            running = 0;
            break;
        }

        /* 临界区：查找已被生产者使用的缓冲区 */
        for (int i = 0; i < BUFFER_COUNT; i++) {
            if (pool->index[i] == 1) {
                /* 找到可消费缓冲区，读取数据 */
                strncpy(text, pool->buffer[i], BUFFER_SIZE - 1);
                text[BUFFER_SIZE - 1] = '\0';
                pool->index[i] = 0;  /* 标记为未被生产者使用 */

                /* 打印并写入文件 */
                printf("[消费者 %d] 从缓冲区[%d] 读取: \"%s\"\n",
                       consumer_id, i, text);
                fprintf(fp, "%s\n", text);
                fflush(fp);

                found = 1;
                total_consumed++;
                break;
            }
        }

        if (!found) {
            printf("[消费者 %d] 缓冲池为空，等待生产者生产...\n", consumer_id);
        }

        /* V 操作：释放互斥权 */
        semaphore_v(semid, SEM_MUTEX);

        loop_count++;

        /* 随机睡眠，模拟消费时间 */
        usleep((rand() % 500 + 100) * 1000);  /* 100ms~600ms */
    }

    printf("[消费者 %d] 结束，共消费 %d 条数据\n", consumer_id, total_consumed);

    /* 清理 */
    shmdt(shm);
    fclose(fp);
    return 0;
}
