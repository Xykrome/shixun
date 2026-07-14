#include "process_server.h"
#include "request_handler.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>

#define REQUEST_DIR "request"
#define OUTPUT_DIR "outputs"

int process_requests(void)
{
    DIR *dir;
    struct dirent *entry;
    char req_path[512];
    char out_path[512];
    char *basename_end;
    int basename_len;
    pid_t *child_pids = NULL;
    int child_count = 0;
    int idx = 0;
    int i;

    /* 第一遍扫描：统计 .req 文件数量 */
    dir = opendir(REQUEST_DIR);
    if (dir == NULL) {
        log_error("Failed to open request directory");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".req") != NULL)
            child_count++;
    }
    closedir(dir);

    if (child_count == 0) {
        log_info("No request files found in request/");
        return 0;
    }

    log_info("Processing requests...");

    child_pids = (pid_t *)malloc(sizeof(pid_t) * child_count);
    if (child_pids == NULL) {
        log_error("Failed to allocate memory for child PIDs");
        return -1;
    }

    dir = opendir(REQUEST_DIR);
    if (dir == NULL) {
        free(child_pids);
        log_error("Failed to open request directory");
        return -1;
    }

    /*
     * 第二遍扫描：为每个 .req 文件 fork 子进程
     *
     * fork 后父子进程执行的分支：
     *   pid == 0  → 子进程：调用 handle_request() 处理请求，然后 _exit(0)
     *   pid > 0   → 父进程：记录子进程 PID，继续循环创建下一个子进程
     */
    while ((entry = readdir(dir)) != NULL) {
        pid_t pid;
        char log_msg[512];

        if (strstr(entry->d_name, ".req") == NULL)
            continue;

        /* 构造请求文件路径 */
        snprintf(req_path, sizeof(req_path), REQUEST_DIR "/%s", entry->d_name);

        /* 构造输出文件路径：将 .req 替换为 .out */
        basename_end = strstr(entry->d_name, ".req");
        basename_len = (int)(basename_end - entry->d_name);
        snprintf(out_path, sizeof(out_path),
                 OUTPUT_DIR "/%.*s.out", basename_len, entry->d_name);

        pid = fork();

        if (pid < 0) {
            log_error("fork failed");
            continue;
        }

        if (pid == 0) {
            /* ===== 子进程分支 =====
             * fork 返回 0，进入此分支。
             * 子进程继承了父进程的文件描述符、内存副本（写时拷贝），
             * 但拥有独立的 PID 和地址空间。
             * 这里负责：打开请求文件 → 解析请求 → 生成响应 → 写入输出文件。
             */
            snprintf(log_msg, sizeof(log_msg),
                     "Child PID=%d handling: %s", getpid(), entry->d_name);
            log_info(log_msg);

            handle_request(req_path, out_path);

            snprintf(log_msg, sizeof(log_msg),
                     "Child PID=%d finished: %s", getpid(), entry->d_name);
            log_info(log_msg);

            log_close();
            _exit(0);  /* 子进程完成任务后立即退出，不返回父进程的循环 */
        }

        /* ===== 父进程分支 =====
         * fork 返回子进程的 PID（>0），进入此分支。
         * 父进程不处理请求，只记录子进程 PID，然后继续创建下一个子进程。
         * 所有子进程创建完成后，父进程在下面的循环中调用 waitpid 等待。
         */
        child_pids[idx++] = pid;
        snprintf(log_msg, sizeof(log_msg),
                 "Parent created child PID=%d for: %s", pid, entry->d_name);
        log_info(log_msg);
    }
    closedir(dir);

    /*
     * 父进程等待所有子进程结束。
     *
     * 为什么要用 waitpid 而不是 wait：
     *   - waitpid 可以指定等待某个具体的 PID，而 wait 等待任意子进程。
     *   - 我们需要精确追踪每个子进程的退出状态，所以用 waitpid 逐个等待。
     *   - 这样日志中能清楚看到哪个子进程以什么状态退出。
     */
    log_info("Parent waiting for all children to finish...");
    for (i = 0; i < idx; i++) {
        int status;
        pid_t result;
        char log_msg[512];

        result = waitpid(child_pids[i], &status, 0);
        if (result > 0) {
            snprintf(log_msg, sizeof(log_msg),
                     "Child PID=%d exited with status=%d",
                     result, WEXITSTATUS(status));
        } else {
            snprintf(log_msg, sizeof(log_msg),
                     "waitpid failed for PID=%d: %s",
                     child_pids[i], strerror(errno));
        }
        log_info(log_msg);
    }

    log_info("All children finished");
    free(child_pids);
    return 0;
}