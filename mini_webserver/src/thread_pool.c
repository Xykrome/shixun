#include "thread_pool.h"
#include "request_handler.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#define RECV_BUF_SIZE 8192
#define RESP_BUF_SIZE 8192

/* ===== 全局线程池状态 ===== */
static work_t queue;                       /* 任务队列 */
static pthread_t *workers = NULL;          /* worker 线程数组 */
static int *worker_ids = NULL;             /* worker 编号数组 */
static int pool_size = 0;                  /* 线程池大小 */

/**
 * queue_init — 初始化任务队列
 */
static void queue_init(void)
{
    queue.head     = 0;
    queue.tail     = 0;
    queue.count    = 0;
    queue.shutdown = 0;
    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.not_empty, NULL);
}

/**
 * queue_destroy — 销毁队列的互斥量和条件变量
 */
static void queue_destroy(void)
{
    pthread_mutex_destroy(&queue.mutex);
    pthread_cond_destroy(&queue.not_empty);
}

/**
 * worker_thread — Worker 线程启动例程
 *
 * 每个 worker 线程循环执行：
 *   1. 等待条件变量（队列非空 或 shutdown）
 *   2. 从队列取一个 client_fd
 *   3. 解锁后调用 handle_request_string 处理 HTTP 请求
 *   4. 发送 HTTP 响应
 *   5. 关闭 client_fd
 *   6. shutdown 且队列空 → 退出循环
 *
 * 对照指导书第5页的流程：
 *   client_fd → worker → handler → close(client_fd)
 */
static void *worker_thread(void *arg)
{
    int worker_id = *(int *)arg;
    char log_msg[512];
    pid_t tid = syscall(SYS_gettid);  /* 获取内核线程 ID */

    snprintf(log_msg, sizeof(log_msg),
             "[V0.8] Worker-%d (TID=%d) started", worker_id, tid);
    log_info(log_msg);
    printf("[V0.8] Worker-%d (TID=%d) started\n", worker_id, tid);

    while (1) {
        int client_fd;

        /* ===== 等待任务 =====
         *
         * 条件等待：队列为空且未 shutdown 时，阻塞等待。
         * pthread_cond_wait 原子性地解锁 mutex 并进入等待，
         * 被唤醒后重新获取 mutex。
         */
        pthread_mutex_lock(&queue.mutex);

        while (queue.count == 0 && !queue.shutdown) {
            pthread_cond_wait(&queue.not_empty, &queue.mutex);
        }

        /* shutdown 且队列为空 → 退出 */
        if (queue.count == 0 && queue.shutdown) {
            pthread_mutex_unlock(&queue.mutex);
            break;
        }

        /* 取出队首 client_fd */
        client_fd = queue.client_fds[queue.head];
        queue.head = (queue.head + 1) % TASK_QUEUE_SIZE;
        queue.count--;

        pthread_mutex_unlock(&queue.mutex);

        /* ===== 处理客户端请求 =====
         *
         * 对照指导书第5页：
         *   worker → handler → close(client_fd)
         *
         * 1. recv 接收 HTTP 请求报文
         * 2. handle_request_string 解析 + 路由 + 生成响应
         * 3. send 发送 HTTP 响应
         * 4. close 关闭客户端连接（由 worker 负责）
         */
        {
            char recv_buf[RECV_BUF_SIZE];
            char resp_buf[RESP_BUF_SIZE];
            ssize_t n;

            memset(recv_buf, 0, sizeof(recv_buf));
            n = recv(client_fd, recv_buf, sizeof(recv_buf) - 1, 0);

            if (n < 0) {
                snprintf(log_msg, sizeof(log_msg),
                         "[V0.8] Worker-%d: recv() failed on fd=%d",
                         worker_id, client_fd);
                log_error(log_msg);
                close(client_fd);
                continue;
            }

            if (n == 0) {
                snprintf(log_msg, sizeof(log_msg),
                         "[V0.8] Worker-%d: client fd=%d closed connection before sending",
                         worker_id, client_fd);
                log_info(log_msg);
                close(client_fd);
                continue;
            }

            recv_buf[n] = '\0';

            snprintf(log_msg, sizeof(log_msg),
                     "[V0.8] Worker-%d: recv() %zd bytes from fd=%d",
                     worker_id, n, client_fd);
            log_info(log_msg);

            /* 解析请求行用于日志 */
            {
                char method[16] = {0};
                char path[256] = {0};
                if (sscanf(recv_buf, "%15s %255s", method, path) >= 2) {
                    snprintf(log_msg, sizeof(log_msg),
                             "[V0.8] Worker-%d handling: %s %s",
                             worker_id, method, path);
                    log_info(log_msg);
                }
            }

            /* 调用已有的 HTTP 请求处理函数（V0.6 引入） */
            memset(resp_buf, 0, sizeof(resp_buf));
            handle_request_string(recv_buf, resp_buf, sizeof(resp_buf));

            /* 发送 HTTP 响应 */
            n = send(client_fd, resp_buf, strlen(resp_buf), 0);
            if (n < 0) {
                snprintf(log_msg, sizeof(log_msg),
                         "[V0.8] Worker-%d: send() failed on fd=%d (client may have disconnected)",
                         worker_id, client_fd);
                log_error(log_msg);
            } else {
                snprintf(log_msg, sizeof(log_msg),
                         "[V0.8] Worker-%d: send() %zd bytes to fd=%d",
                         worker_id, n, client_fd);
                log_info(log_msg);

                /* 提取响应状态行用于日志 */
                {
                    char status_line[64] = {0};
                    if (sscanf(resp_buf, "HTTP/1.1 %63[^\r\n]", status_line) == 1) {
                        snprintf(log_msg, sizeof(log_msg),
                                 "[V0.8] Worker-%d: response status → %s",
                                 worker_id, status_line);
                        log_info(log_msg);
                    }
                }
            }

            /* Worker 负责关闭客户端连接（对照指导书第5页第⑦步） */
            close(client_fd);
            snprintf(log_msg, sizeof(log_msg),
                     "[V0.8] Worker-%d: closed client fd=%d",
                     worker_id, client_fd);
            log_info(log_msg);
        }
    }

    snprintf(log_msg, sizeof(log_msg),
             "[V0.8] Worker-%d (TID=%d) exiting", worker_id, tid);
    log_info(log_msg);
    printf("[V0.8] Worker-%d (TID=%d) exiting\n", worker_id, tid);
    return NULL;
}

/**
 * thread_pool_init — 初始化线程池
 *
 * 创建 num_workers 个 worker 线程，所有线程启动后阻塞等待任务。
 */
int thread_pool_init(int num_workers)
{
    int i;
    char log_msg[512];

    if (num_workers < 1)
        num_workers = 4;  /* 默认 4 个 worker */

    pool_size = num_workers;

    /* 初始化任务队列 */
    queue_init();

    /* 分配线程数组 */
    workers    = (pthread_t *)malloc(sizeof(pthread_t) * num_workers);
    worker_ids = (int *)malloc(sizeof(int) * num_workers);

    if (workers == NULL || worker_ids == NULL) {
        log_error("[V0.8] Failed to allocate memory for thread pool");
        free(workers);
        free(worker_ids);
        workers = NULL;
        worker_ids = NULL;
        return -1;
    }

    snprintf(log_msg, sizeof(log_msg),
             "[V0.8] Creating thread pool with %d worker threads", num_workers);
    log_info(log_msg);
    printf("[V0.8] Creating thread pool with %d workers\n", num_workers);

    /* 创建 worker 线程 */
    for (i = 0; i < num_workers; i++) {
        worker_ids[i] = i;
        if (pthread_create(&workers[i], NULL, worker_thread, &worker_ids[i]) != 0) {
            log_error("[V0.8] Failed to create worker thread");
            /* 设置 shutdown 并唤醒已创建的线程，让它们退出 */
            pthread_mutex_lock(&queue.mutex);
            queue.shutdown = 1;
            pthread_cond_broadcast(&queue.not_empty);
            pthread_mutex_unlock(&queue.mutex);
            /* 等待已创建的线程退出 */
            for (int j = 0; j < i; j++) {
                pthread_join(workers[j], NULL);
            }
            free(workers);
            free(worker_ids);
            workers = NULL;
            worker_ids = NULL;
            queue_destroy();
            return -1;
        }
    }

    return 0;
}

/**
 * thread_pool_add_task — 向线程池添加任务
 *
 * 将 client_fd 加入任务队列并唤醒一个等待的 worker。
 * 主线程在 accept 后调用此函数。
 *
 * 返回值：0 成功，-1 队列已满或线程池已关闭
 */
int thread_pool_add_task(int client_fd)
{
    pthread_mutex_lock(&queue.mutex);

    if (queue.shutdown) {
        pthread_mutex_unlock(&queue.mutex);
        return -1;
    }

    if (queue.count >= TASK_QUEUE_SIZE) {
        pthread_mutex_unlock(&queue.mutex);
        return -1;
    }

    /* 将 client_fd 放入队尾 */
    queue.client_fds[queue.tail] = client_fd;
    queue.tail = (queue.tail + 1) % TASK_QUEUE_SIZE;
    queue.count++;

    /* 唤醒一个等待的 worker 线程 */
    pthread_cond_signal(&queue.not_empty);
    pthread_mutex_unlock(&queue.mutex);

    return 0;
}

int thread_pool_destroy(void)
{


    
    int i;
    char log_msg[512];

    if (workers == NULL)
        return 0;

    log_info("[V0.8] Shutting down thread pool...");
    printf("[V0.8] Shutting down thread pool...\n");

    /* 设置 shutdown 标志，广播唤醒所有等待的 worker */
    pthread_mutex_lock(&queue.mutex);
    queue.shutdown = 1;
    pthread_cond_broadcast(&queue.not_empty);
    pthread_mutex_unlock(&queue.mutex);

    /* 等待所有 worker 线程结束（类比 V0.4 的 waitpid / V0.5 的 pthread_join） */
    for (i = 0; i < pool_size; i++) {
        void *retval;
        pthread_join(workers[i], &retval);
        snprintf(log_msg, sizeof(log_msg),
                 "[V0.8] Worker-%d joined", i);
        log_info(log_msg);
    }

    log_info("[V0.8] All workers exited, thread pool destroyed");
    printf("[V0.8] All workers exited, thread pool destroyed\n");

    /* 清理 */
    free(workers);
    free(worker_ids);
    workers = NULL;
    worker_ids = NULL;
    pool_size = 0;

    queue_destroy();

    return 0;
}
