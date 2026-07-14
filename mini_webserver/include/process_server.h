#ifndef PROCESS_SERVER_H
#define PROCESS_SERVER_H

/*
 * 扫描 request/ 目录中的所有 .req 文件，为每个请求创建一个子进程处理。
 * 父进程等待所有子进程结束后返回。
 *
 * fork 后父子进程分别执行：
 *   - 子进程：调用 handle_request() 处理请求，写入输出文件，然后 _exit(0) 退出。
 *   - 父进程：记录子进程 PID，继续扫描下一个请求文件并创建新的子进程。
 *     所有子进程创建完后，父进程通过 waitpid() 逐个等待子进程结束，
 *     记录每个子进程的退出状态。
 *
 * 为什么用 waitpid 而不是 wait：
 *   waitpid 可以等待特定 PID 的子进程，而 wait 只能等待任意一个子进程。
 *   当需要精确追踪每个子进程的退出状态时，waitpid 是唯一的选择。
 *   此外 waitpid 支持 WNOHANG 选项实现非阻塞等待，更灵活。
 *
 * 返回值：成功返回 0，失败返回 -1
 */
int process_requests(void);

#endif