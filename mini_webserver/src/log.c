/*
 * W3D2 log.c — 增强日志系统实现
 *
 * 功能：
 *   1. 系统日志：记录服务器运行状态、错误和调试信息
 *      - 支持四级日志：DEBUG / INFO / WARNING / ERROR
 *      - 格式：YYYY-MM-DD HH:MM:SS [LEVEL] 消息
 *   2. 访问日志：记录每个 HTTP 请求的处理结果
 *      - 格式：YYYY-MM-DD HH:MM:SS IP METHOD PATH HTTP-VERSION STATUS MIME BYTES
 *
 * 对照 W3D2 验收标准：
 *   - 系统日志和访问日志分别写入不同文件
 *   - 访问日志包含：时间、客户端IP、方法、URL、MIME、状态码、响应字节数
 *   - 系统日志包含：时间、级别、事件描述
 *   - 支持 DEBUG、INFO、WARNING、ERROR 四级
 */

#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

static FILE *sys_log_fp    = NULL;  /* 系统日志文件 */
static FILE *access_log_fp = NULL;  /* 访问日志文件 */

/*
 * 获取当前时间字符串，格式：YYYY-MM-DD HH:MM:SS
 */
static void get_timestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/*
 * 写入系统日志的内部函数
 */
static void sys_log_write(const char *level, const char *message)
{
    char timestamp[64];

    if (sys_log_fp == NULL) {
        return;
    }

    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(sys_log_fp, "%s [%s] %s\n", timestamp, level, message);
    fflush(sys_log_fp);
}

int log_init(const char *sys_log_path, const char *access_log_path)
{
    int result = 0;

    /* 打开系统日志文件（追加模式） */
    if (sys_log_path != NULL) {
        sys_log_fp = fopen(sys_log_path, "a");
        if (sys_log_fp == NULL) {
            fprintf(stderr, "[LOG] Warning: failed to open system log: %s\n",
                    sys_log_path);
            result = -1;
        } else {
            /* 写入日志文件头，标记一次新的服务器运行 */
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            fprintf(sys_log_fp, "%s [INFO] ===== Log session started (PID=%d) =====\n",
                    timestamp, getpid());
            fflush(sys_log_fp);
        }
    }

    /* 打开访问日志文件（追加模式） */
    if (access_log_path != NULL) {
        access_log_fp = fopen(access_log_path, "a");
        if (access_log_fp == NULL) {
            fprintf(stderr, "[LOG] Warning: failed to open access log: %s\n",
                    access_log_path);
            result = -1;
        }
    }

    return result;
}

void log_debug(const char *message)
{
    sys_log_write("DEBUG", message);
}

void log_info(const char *message)
{
    sys_log_write("INFO", message);
}

void log_warning(const char *message)
{
    sys_log_write("WARNING", message);
}

void log_error(const char *message)
{
    sys_log_write("ERROR", message);
}

void access_log(const char *client_ip, const char *method,
                const char *path, const char *http_version,
                int status_code, const char *mime_type, int response_size)
{
    char timestamp[64];

    if (access_log_fp == NULL) {
        return;
    }

    get_timestamp(timestamp, sizeof(timestamp));

    /*
     * 访问日志格式（V1.2 增强）：
     *   时间 IP 方法 URL HTTP版本 状态码 MIME 响应字节数
     *
     * 示例：
     *   2026-07-19 09:05:21 127.0.0.1 GET / HTTP/1.1 200 text/html; charset=utf-8 1234
     */
    fprintf(access_log_fp, "%s %s %s %s %s %d %s %d\n",
            timestamp,
            client_ip,
            method,
            path,
            http_version,
            status_code,
            mime_type ? mime_type : "-",
            response_size);
    fflush(access_log_fp);
}

void log_close(void)
{
    if (sys_log_fp != NULL) {
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        fprintf(sys_log_fp, "%s [INFO] ===== Log session ended =====\n",
                timestamp);
        fclose(sys_log_fp);
        sys_log_fp = NULL;
    }

    if (access_log_fp != NULL) {
        fclose(access_log_fp);
        access_log_fp = NULL;
    }
}
