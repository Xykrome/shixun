#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

/*
 * V0.8: 线程池模块
 *
 * 提供固定大小的线程池，用于处理 TCP 客户端连接。
 *
 * 架构：
 *   1. 预创建 N 个 worker 线程，全部阻塞在条件变量上等待任务
 *   2. 主线程 accept 后将 client_fd 放入任务队列，唤醒一个 worker
 *   3. worker 取出 client_fd，处理 HTTP 请求，发送响应，关闭连接
 *   4. 服务器关闭时设置 shutdown 标志，广播唤醒所有 worker，pthread_join 回收
 *
 * 任务队列是一个环形缓冲区（client_fds[64]），由 mutex + 条件变量保护。
 *
 * 同步机制：
 *   - mutex     保护队列的并发访问
 *   - not_empty 条件变量：worker 等待队列非空（或 shutdown 通知）
 */

#define TASK_QUEUE_SIZE 64

/* ===== 任务队列（生产者-消费者） ===== */
typedef struct {
    int client_fds[TASK_QUEUE_SIZE];  /* 环形缓冲区：存放客户端套接字描述符 */
    int head;                          /* 队首索引（worker 取走位置） */
    int tail;                          /* 队尾索引（主线程放入位置） */
    int count;                         /* 当前队列中的任务数 */
    int shutdown;                      /* 线程池关闭标志（1=关闭，worker 应退出） */
    pthread_mutex_t mutex;             /* 保护队列的互斥量 */
    pthread_cond_t  not_empty;         /* 条件变量：队列非空 / shutdown 通知 */
} work_t;

/*
 * thread_pool_init — 初始化线程池
 *
 * 创建 num_workers 个 worker 线程，初始化任务队列。
 * 每个 worker 线程启动后立即阻塞在条件变量上等待任务。
 *
 * 参数：
 *   num_workers - worker 线程数量（建议 4~8）
 *
 * 返回值：成功返回 0，失败返回 -1
 */
int thread_pool_init(int num_workers);

/*
 * thread_pool_add_task — 向线程池添加任务（主线程调用）
 *
 * 将 client_fd 放入任务队列，并唤醒一个等待的 worker 线程。
 *
 * 参数：
 *   client_fd - 已 accept 的客户端套接字描述符
 *
 * 返回值：成功返回 0，队列满返回 -1
 */
int thread_pool_add_task(int client_fd);

/*
 * thread_pool_destroy — 关闭并销毁线程池
 *
 * 1. 设置 shutdown 标志
 * 2. 广播条件变量，唤醒所有阻塞等待的 worker
 * 3. pthread_join 等待所有 worker 线程退出
 * 4. 销毁 mutex 和条件变量
 *
 * 返回值：成功返回 0
 */
int thread_pool_destroy(void);

#endif
