/**
 * thread_server.c — V0.5 多线程请求处理
 *
 * 使用 POSIX 线程 (pthread) + 共享请求队列实现并发请求处理。
 *
 * 同步设计：
 *   - 请求队列由 mutex + 条件变量保护，实现生产者-消费者模式
 *   - 主线程（生产者）：扫描 request/ 目录，将任务入队，signal 条件变量
 *   - Worker 线程（消费者）：等待条件变量 → 取任务 → 处理 → 更新统计
 *   - 统计数据由独立的 mutex 保护
 *
 * 与 V0.4 多进程版本对比：
 *   V0.4: fork + exec 进程隔离，COW 内存共享，waitpid 等待
 *   V0.5: pthread 线程共享内存，mutex + cond 同步，pthread_join 等待
 */

#include "thread_server.h"
#include "request_handler.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/syscall.h>

#define REQUEST_DIR   "request"
#define OUTPUT_DIR    "outputs"
#define QUEUE_CAPACITY 256

/* ===== 单个任务 ===== */
typedef struct {
    char req_path[512];
    char out_path[512];
} task_t;

/* ===== 共享请求队列（生产者-消费者） ===== */
typedef struct {
    task_t tasks[QUEUE_CAPACITY];     /* 环形缓冲区 */
    int front;                        /* 队首索引（消费者取走位置） */
    int rear;                         /* 队尾索引（生产者放入位置） */
    int count;                        /* 当前队列中的任务数 */
    int all_enqueued;                 /* 生产者完成标志（1=不再有新任务） */
    pthread_mutex_t mutex;            /* 保护队列的互斥量 */
    pthread_cond_t  cond;             /* 条件变量：队列非空 / 全部完成 */
} request_queue_t;

/* ===== 共享统计信息 ===== */
typedef struct {
    int processed;                    /* 已处理任务总数 */
    pthread_mutex_t mutex;            /* 保护统计数据的互斥量 */
} worker_stats_t;

static request_queue_t queue;
static worker_stats_t   stats;

/**
 * queue_init — 初始化请求队列
 */
static void queue_init(void)
{
    queue.front         = 0;
    queue.rear          = 0;
    queue.count         = 0;
    queue.all_enqueued  = 0;
    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.cond, NULL);
}

/**
 * queue_enqueue — 将任务加入队列（生产者调用）
 *   返回 0 成功，-1 队列已满
 */
static int queue_enqueue(const char *req_path, const char *out_path)
{
    pthread_mutex_lock(&queue.mutex);

    if (queue.count >= QUEUE_CAPACITY) {
        pthread_mutex_unlock(&queue.mutex);
        return -1;  /* 队列已满 */
    }

    snprintf(queue.tasks[queue.rear].req_path,
             sizeof(queue.tasks[queue.rear].req_path),
             "%s", req_path);
    snprintf(queue.tasks[queue.rear].out_path,
             sizeof(queue.tasks[queue.rear].out_path),
             "%s", out_path);

    queue.rear = (queue.rear + 1) % QUEUE_CAPACITY;
    queue.count++;

    /* 唤醒一个等待的消费者线程 */
    pthread_cond_signal(&queue.cond);
    pthread_mutex_unlock(&queue.mutex);

    return 0;
}

/**
 * queue_dequeue — 从队列取出任务（消费者调用）
 *   队列非空时取出一个任务返回 1
 *   队列空且 all_enqueued 时返回 0（消费者应退出）
 *   队列空但 all_enqueued 为 0 时阻塞等待（条件变量）
 */
static int queue_dequeue(task_t *out)
{
    pthread_mutex_lock(&queue.mutex);

    /*
     * 条件等待：队列为空且生产者尚未完成时，阻塞等待。
     * pthread_cond_wait 会原子性地解锁 mutex 并进入等待，
     * 被唤醒后重新获取 mutex。
     */
    while (queue.count == 0 && !queue.all_enqueued) {
        pthread_cond_wait(&queue.cond, &queue.mutex);
    }

    /* 队列空 + 生产者已完成 → 没有更多任务了 */
    if (queue.count == 0 && queue.all_enqueued) {
        pthread_mutex_unlock(&queue.mutex);
        return 0;
    }

    /* 取出队首任务 */
    *out = queue.tasks[queue.front];
    queue.front = (queue.front + 1) % QUEUE_CAPACITY;
    queue.count--;

    pthread_mutex_unlock(&queue.mutex);
    return 1;
}

/**
 * queue_signal_all_done — 生产者标记所有任务已入队
 *   广播条件变量，唤醒所有等待的消费者线程
 */
static void queue_signal_all_done(void)
{
    pthread_mutex_lock(&queue.mutex);
    queue.all_enqueued = 1;
    pthread_cond_broadcast(&queue.cond);  /* 唤醒所有等待的 worker */
    pthread_mutex_unlock(&queue.mutex);
}

/**
 * queue_destroy — 销毁队列的互斥量和条件变量
 */
static void queue_destroy(void)
{
    pthread_mutex_destroy(&queue.mutex);
    pthread_cond_destroy(&queue.cond);
}

/**
 * stats_init — 初始化统计互斥量
 */
static void stats_init(void)
{
    stats.processed = 0;
    pthread_mutex_init(&stats.mutex, NULL);
}

/**
 * stats_increment — 线程安全地增加已处理计数
 */
static void stats_increment(void)
{
    pthread_mutex_lock(&stats.mutex);
    stats.processed++;
    pthread_mutex_unlock(&stats.mutex);
}

/**
 * stats_destroy — 销毁统计互斥量
 */
static void stats_destroy(void)
{
    pthread_mutex_destroy(&stats.mutex);
}

/**
 * worker_thread — Worker 线程启动例程
 *
 *   每个 worker 线程循环执行：
 *     1. 等待条件变量（队列非空 或 全部完成）
 *     2. 从队列取一个任务
 *     3. 解锁后调用 handle_request 处理请求
 *     4. 线程安全地更新已处理计数
 *     5. 队列空且生产者完成 → 退出循环
 */
static void *worker_thread(void *arg)
{
    int worker_id = *(int *)arg;
    task_t task;
    char log_msg[512];
    pid_t tid = syscall(SYS_gettid);  /* 获取内核线程 ID */

    snprintf(log_msg, sizeof(log_msg),
             "Worker-%d (TID=%d) started", worker_id, tid);
    log_info(log_msg);

    while (queue_dequeue(&task)) {
        /* 处理请求（与 V0.4 子进程调用相同的 handle_request） */
        snprintf(log_msg, sizeof(log_msg),
                 "Worker-%d (TID=%d) handling: %.400s", worker_id, tid, task.req_path);
        log_info(log_msg);

        handle_request(task.req_path, task.out_path);

        snprintf(log_msg, sizeof(log_msg),
                 "Worker-%d (TID=%d) finished: %.400s", worker_id, tid, task.req_path);
        log_info(log_msg);

        /* 更新统计 */
        stats_increment();
    }

    snprintf(log_msg, sizeof(log_msg),
             "Worker-%d (TID=%d) exiting", worker_id, tid);
    log_info(log_msg);
    return NULL;
}

/**
 * thread_requests — V0.5 多线程请求处理入口
 *
 *   流程：
 *     1. 初始化共享队列和统计数据
 *     2. 创建 num_workers 个 worker 线程
 *     3. 主线程扫描 request/ 目录，将所有 .req 任务入队
 *     4. 标记入队完成，广播唤醒所有 worker
 *     5. 主线程 pthread_join 等待所有 worker 结束
 *     6. 输出统计信息，清理资源
 */
int thread_requests(int num_workers)
{
    DIR *dir;
    struct dirent *entry;
    char req_path[512];
    char out_path[512];
    char *basename_end;
    int basename_len;
    int enqueued;
    pthread_t *workers;
    int *worker_ids;
    int i;
    char log_msg[512];

    if (num_workers < 1)
        num_workers = 4;  /* 默认 4 个 worker */

    /* 初始化 */
    queue_init();
    stats_init();

    /* 创建 worker 线程 */
    workers    = (pthread_t *)malloc(sizeof(pthread_t) * num_workers);
    worker_ids = (int *)malloc(sizeof(int) * num_workers);

    snprintf(log_msg, sizeof(log_msg),
             "V0.5 Multi-threaded server: creating %d worker threads", num_workers);
    log_info(log_msg);

    for (i = 0; i < num_workers; i++) {
        worker_ids[i] = i;
        if (pthread_create(&workers[i], NULL, worker_thread, &worker_ids[i]) != 0) {
            log_error("Failed to create worker thread");
            /* 标记入队完成，让已创建的线程退出 */
            queue_signal_all_done();
            for (int j = 0; j < i; j++)
                pthread_join(workers[j], NULL);
            free(workers);
            free(worker_ids);
            queue_destroy();
            stats_destroy();
            return -1;
        }
    }

    /*
     * 主线程作为生产者：扫描 request/ 目录，将任务入队
     * 每入队一个任务就 signal 条件变量，唤醒一个 worker
     */
    dir = opendir(REQUEST_DIR);
    if (dir == NULL) {
        log_error("Failed to open request directory");
        queue_signal_all_done();
        for (i = 0; i < num_workers; i++)
            pthread_join(workers[i], NULL);
        free(workers);
        free(worker_ids);
        queue_destroy();
        stats_destroy();
        return -1;
    }

    enqueued = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".req") == NULL)
            continue;

        /* 构造请求文件路径 */
        snprintf(req_path, sizeof(req_path), REQUEST_DIR "/%s", entry->d_name);

        /* 构造输出文件路径：将 .req 替换为 .out */
        basename_end = strstr(entry->d_name, ".req");
        basename_len = (int)(basename_end - entry->d_name);
        snprintf(out_path, sizeof(out_path),
                 OUTPUT_DIR "/%.*s.out", basename_len, entry->d_name);

        if (queue_enqueue(req_path, out_path) == 0) {
            snprintf(log_msg, sizeof(log_msg),
                     "Main thread enqueued: %.200s → %.200s", entry->d_name, out_path);
            log_info(log_msg);
            enqueued++;
        } else {
            snprintf(log_msg, sizeof(log_msg),
                     "Queue full, skipping: %s", entry->d_name);
            log_error(log_msg);
        }
    }
    closedir(dir);

    snprintf(log_msg, sizeof(log_msg),
             "Main thread: %d tasks enqueued, signaling completion", enqueued);
    log_info(log_msg);

    /* 生产者完成入队，广播唤醒所有等待的 worker */
    queue_signal_all_done();

    /*
     * 主线程等待所有 worker 线程结束。
     * 与 V0.4 的 waitpid 对应，这里使用 pthread_join。
     *
     * pthread_join 与 waitpid 的类比：
     *   waitpid(pid, &status, 0)  → 等待特定子进程结束
     *   pthread_join(tid, &retval) → 等待特定线程结束
     *   都用于回收子执行体的资源和退出状态。
     */
    log_info("Main thread waiting for all workers to finish...");
    for (i = 0; i < num_workers; i++) {
        void *retval;
        pthread_join(workers[i], &retval);
        snprintf(log_msg, sizeof(log_msg),
                 "Worker-%d joined", i);
        log_info(log_msg);
    }

    /* 输出统计 */
    snprintf(log_msg, sizeof(log_msg),
             "All workers finished. Total processed: %d of %d enqueued",
             stats.processed, enqueued);
    log_info(log_msg);

    /* 清理 */
    free(workers);
    free(worker_ids);
    queue_destroy();
    stats_destroy();

    return 0;
}
