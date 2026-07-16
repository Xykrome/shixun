/*
 * W2D5 main.c — epoll 聊天服务器入口
 *
 * 用法：
 *   ./epoll_server [port]
 *
 * 示例：
 *   ./epoll_server 8888     # 监听 8888 端口（默认）
 *   ./epoll_server           # 使用默认端口 8888
 */

#include "epoll_server.h"
#include <stdio.h>
#include <stdlib.h>

static void print_usage(const char *prog)
{
    printf("W2D5 — Epoll-based Multi-Client Chat Server\n");
    printf("\n");
    printf("Usage: %s [port]\n", prog);
    printf("\n");
    printf("Examples:\n");
    printf("  %s                Start on default port %d\n", prog, DEFAULT_PORT);
    printf("  %s 8888           Start on port 8888\n", prog);
    printf("  %s 9999           Start on port 9999\n", prog);
}

int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;

    if (argc > 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (argc == 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Error: Invalid port '%s'\n", argv[1]);
            return 1;
        }
    }

    printf("=== W2D5 Epoll Chat Server ===\n");
    return epoll_server_run(port);
}
