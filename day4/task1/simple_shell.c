/*
 * Task1: 简单 Shell 程序
 * 使用 POSIX API (fork/exec/waitpid) 实现
 *
 * 功能：
 * - 读取用户输入的命令
 * - 创建子进程执行命令（fork + exec）
 * - 父进程等待子进程结束（waitpid）
 * - 支持 exit 命令退出
 * - 支持带参数的命令
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_CMD_LEN  256     /* 最大命令长度 */
#define MAX_ARGS     32      /* 最大参数个数 */

int main(void)
{
    char cmd_line[MAX_CMD_LEN];
    char *args[MAX_ARGS];
    pid_t pid;

    printf("========================================\n");
    printf("   简易 Shell 程序 (Task1)\n");
    printf("   输入 'exit' 退出\n");
    printf("========================================\n\n");

    while (1) {
        /* 显示提示符 */
        printf("myshell> ");
        fflush(stdout);

        /* 读取用户输入 */
        if (fgets(cmd_line, MAX_CMD_LEN, stdin) == NULL) {
            printf("\n");
            break;
        }

        /* 去除末尾换行符 */
        cmd_line[strcspn(cmd_line, "\n")] = '\0';

        /* 跳过空行 */
        if (strlen(cmd_line) == 0) {
            continue;
        }

        /* 解析命令行为参数数组 */
        int argc = 0;
        char *token = strtok(cmd_line, " ");
        while (token != NULL && argc < MAX_ARGS - 1) {
            args[argc++] = token;
            token = strtok(NULL, " ");
        }
        args[argc] = NULL;

        if (argc == 0) {
            continue;
        }

        /* 处理 exit 命令 */
        if (strcmp(args[0], "exit") == 0) {
            printf("退出 Shell。再见！\n");
            break;
        }

        /* 处理 cd 命令（内置命令，需要特殊处理） */
        if (strcmp(args[0], "cd") == 0) {
            if (argc < 2) {
                fprintf(stderr, "cd: 缺少参数\n");
            } else if (chdir(args[1]) != 0) {
                perror("cd 失败");
            }
            continue;
        }

        /* 创建子进程 */
        pid = fork();
        if (pid < 0) {
            perror("fork 失败");
            continue;
        }

        if (pid == 0) {
            /* 子进程：执行命令 */
            printf("[子进程] PID=%d, 执行命令: %s\n", getpid(), args[0]);
            execvp(args[0], args);

            /* execvp 成功则不返回，到这里说明执行失败 */
            fprintf(stderr, "命令执行失败: %s\n", args[0]);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else {
            /* 父进程：等待子进程结束 */
            int child_status;
            pid_t child_pid;

            printf("[父进程] 创建子进程 PID=%d, 等待其结束...\n", pid);

            /* 使用 waitpid 等待特定子进程终止 */
            child_pid = waitpid(pid, &child_status, 0);

            if (child_pid == -1) {
                perror("waitpid 失败");
            } else {
                if (WIFEXITED(child_status)) {
                    printf("[父进程] 子进程 PID=%d 正常结束, 退出码=%d\n",
                           child_pid, WEXITSTATUS(child_status));
                } else if (WIFSIGNALED(child_status)) {
                    printf("[父进程] 子进程 PID=%d 被信号终止, 信号=%d\n",
                           child_pid, WTERMSIG(child_status));
                }
            }
            printf("\n");
        }
    }

    return 0;
}
