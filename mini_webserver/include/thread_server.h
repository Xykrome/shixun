#ifndef THREAD_SERVER_H
#define THREAD_SERVER_H

/*
 * V0.5 多线程请求处理
 *
 * 与 V0.4 多进程架构的区别：
 *   V0.4: 父进程 fork 子进程 → 子进程 handle_request → _exit
 *         进程间地址空间隔离，资源开销大，但天然互不干扰。
 *   V0.5: 主线程扫描请求 → 放入共享队列 → 多个 worker 线程取任务处理
 *         线程共享内存，通信方便，但需要互斥量/条件变量保护共享数据。
 *
 * 架构：
 *   1. 主线程扫描 request/ 目录，将所有 .req 文件路径放入共享队列
 *   2. 主线程创建 num_workers 个 worker 线程
 *   3. 每个 worker 线程循环：从队列取任务 → handle_request → 更新统计
 *   4. 所有任务处理完后，worker 线程退出
 *   5. 主线程通过 pthread_join 等待所有 worker 结束
 *
 * 同步机制：
 *   - queue_mutex 保护请求队列的并发访问
 *   - queue_cond   条件变量：worker 等待队列非空 / 任务全部完成
 *   - stats_mutex  保护统计计数器的并发更新
 *
 * 参数：
 *   num_workers - worker 线程数量（建议 3~8）
 *
 * 返回值：成功返回 0，失败返回 -1
 */
int thread_requests(int num_workers);

#endif
