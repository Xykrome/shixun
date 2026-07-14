#include "log.h"
#include <stdio.h>
#include <unistd.h>

static FILE *log_file = NULL;

int log_init(const char *path)
{
    log_file = fopen(path, "a");
    if (log_file == NULL) {
        return -1;
    }
    return 0;
}

void log_info(const char *message)
{
    if (log_file != NULL) {
        /* 日志中记录 PID，便于区分父子进程的输出 */
        fprintf(log_file, "[INFO] PID=%d %s\n", getpid(), message);
        fflush(log_file);
    }
}

void log_error(const char *message)
{
    if (log_file != NULL) {
        fprintf(log_file, "[ERROR] PID=%d %s\n", getpid(), message);
        fflush(log_file);
    }
}

void log_close(void)
{
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}