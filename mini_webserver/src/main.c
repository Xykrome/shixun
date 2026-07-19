#include "config.h"
#include "epoll_server.h"
#include "http_response.h"
#include "http_server.h"
#include "log.h"
#include "process_server.h"
#include "request_handler.h"
#include "tcp_server.h"
#include "tcp_fork_server.h"
#include "tcp_pool_server.h"
#include "thread_server.h"
#include "user_store.h"
#include "user_index.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  Server modes:\n");
    printf("    %s <config_file>                - Start multi-process server (V0.4)\n", prog);
    printf("    %s --thread <config_file>       - Start multi-threaded server (V0.5)\n", prog);
    printf("    %s --thread <config_file> <N>   - Start multi-threaded server with N workers\n", prog);
    printf("    %s --tcp <config_file>          - Start TCP server, single connection (V0.6)\n", prog);
    printf("    %s --fork <config_file>         - Start TCP server, multi-process (V0.7)\n", prog);
    printf("    %s --pool <config_file>         - Start TCP server, thread pool (V0.8)\n", prog);
    printf("    %s --pool <config_file> <N>     - Start TCP server with N worker threads (V0.8)\n", prog);
    printf("    %s serve-epoll <max_requests>   - Start epoll HTTP server (V1.0, W2D5)\n", prog);
    printf("    %s serve-http <max_requests>   - Start epoll HTTP server (V1.3, W3D3)\n", prog);
    printf("\n");
    printf("  User management:\n");
    printf("    %s list                         - List all users (linked list)\n", prog);
    printf("    %s find <username>              - Find a user in linked list\n", prog);
    printf("    %s add <username> <password> <phone> - Add a user\n", prog);
    printf("    %s delete <username>            - Delete a user\n", prog);
    printf("    %s index                        - Show all usernames in index (BST inorder)\n", prog);
    printf("    %s find-index <username>        - Find a user using BST index\n", prog);
    printf("    %s compare <username>           - Compare search steps between list and index\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* 用户管理命令分支（需要加载用户数据） */
    if (strcmp(argv[1], "list") == 0 || strcmp(argv[1], "find") == 0 ||
        strcmp(argv[1], "add") == 0 || strcmp(argv[1], "delete") == 0 ||
        strcmp(argv[1], "index") == 0 || strcmp(argv[1], "find-index") == 0 ||
        strcmp(argv[1], "compare") == 0) {

        user_node_t *users = NULL;
        if (load_users("data/users.csv", &users) != 0) {
            printf("Failed to load users from data/users.csv\n");
            return 1;
        }

        /* 构建索引（用于需要索引的命令） */
        user_index_t index;
        user_index_build(&index, users);

        int ret = 0;

        if (strcmp(argv[1], "list") == 0) {
            list_users(users);
        } else if (strcmp(argv[1], "find") == 0) {
            if (argc < 3) {
                printf("find requires username\n");
                ret = 1;
                goto cleanup;
            }
            printf("%s\n", find_user(users, argv[2]) ? "FOUND" : "NOT_FOUND");
        } else if (strcmp(argv[1], "add") == 0) {
            if (argc < 5) {
                printf("add requires username password phone\n");
                ret = 1;
                goto cleanup;
            }
            int add_ret = add_user(&users, argv[2], argv[3], argv[4]);
            if (add_ret == 0) {
                // 添加成功后保存到文件，并重建索引
                if (save_users("data/users.csv", users) == 0) {
                    printf("User added\n");
                    // 重建索引
                    user_index_free(&index);
                    user_index_build(&index, users);
                } else {
                    printf("User added, but failed to save to file\n");
                }
            } else {
                printf("Add failed: user already exists or error\n");
            }
        } else if (strcmp(argv[1], "delete") == 0) {
            if (argc < 3) {
                printf("delete requires username\n");
                ret = 1;
                goto cleanup;
            }
            int del_ret = delete_user(&users, argv[2]);
            if (del_ret == 0) {
                if (save_users("data/users.csv", users) == 0) {
                    printf("User deleted\n");
                    // 重建索引
                    user_index_free(&index);
                    user_index_build(&index, users);
                } else {
                    printf("User deleted, but failed to save to file\n");
                }
            } else {
                printf("Delete failed: user not found\n");
            }
        } else if (strcmp(argv[1], "index") == 0) {
            printf("Usernames in BST index (inorder):\n");
            user_index_inorder(&index);
        } else if (strcmp(argv[1], "find-index") == 0) {
            if (argc < 3) {
                printf("find-index requires username\n");
                ret = 1;
                goto cleanup;
            }
            user_node_t *found = user_index_find(&index, argv[2]);
            printf("%s\n", found ? "FOUND" : "NOT_FOUND");
        } else if (strcmp(argv[1], "compare") == 0) {
            if (argc < 3) {
                printf("compare requires username\n");
                ret = 1;
                goto cleanup;
            }
            user_index_compare(&index, users, argv[2]);
        }

cleanup:
        free_users(users);
        user_index_free(&index);
        return ret;
    }

    /*
     * ===== Webserver V1.0 (W2D5): Epoll HTTP 服务器 =====
     *
     * 命令格式：./mini_web_server serve-epoll <max_requests>
     *
     * 纯 epoll + 单线程事件循环，不依赖 select/多线程/多进程。
     */
    if (argc >= 3 && strcmp(argv[1], "serve-epoll") == 0) {
        int max_requests = atoi(argv[2]);
        if (max_requests <= 0) {
            fprintf(stderr, "Error: max_requests must be a positive integer\n");
            return 1;
        }

        /* 日志初始化（可选，即使失败也继续运行） */
        if (log_init("logs/server.log", NULL) != 0) {
            fprintf(stderr, "Warning: failed to open log file, continuing without logging\n");
        }

        printf("Starting V1.0 epoll webserver (max %d requests)...\n", max_requests);
        if (epoll_server_run(8080, max_requests) < 0) {
            log_error("failed to start epoll server");
            log_close();
            return 1;
        }

        log_close();
        return 0;
    }

    /*
     * ===== Webserver V1.3 (W3D3): Epoll HTTP 动态查询服务器 =====
     *
     * 命令格式：./mini_web_server serve-http <max_requests>
     *
     * 支持静态文件服务 + 动态学生信息查询：
     *   GET/POST /search → 动态查询（URL 解码 + 参数校验 + 数据文件查询）
     *   GET *             → 静态文件服务
     *   POST /echo        → 200 OK + 回显请求体
     *   其他方法           → 405 Method Not Allowed
     *   系统日志 + 访问日志（含 MIME 类型）分别记录
     *
     * 纯 epoll + 单线程事件循环，不依赖 select/多线程/多进程。
     */
    if (argc >= 3 && strcmp(argv[1], "serve-http") == 0) {
        int max_requests = atoi(argv[2]);
        if (max_requests <= 0) {
            fprintf(stderr, "Error: max_requests must be a positive integer\n");
            return 1;
        }

        /* 日志初始化：系统日志 + 访问日志分别写入 */
        if (log_init("logs/system.log", "logs/access.log") != 0) {
            fprintf(stderr, "Warning: failed to open log files, continuing without logging\n");
        }

        printf("Starting V1.3 epoll HTTP search server (max %d requests)...\n", max_requests);
        if (http_server_run(8080, max_requests) < 0) {
            log_error("failed to start http server");
            log_close();
            return 1;
        }

        log_close();
        return 0;
    }

    /* ===== Web服务器模式 =====
     *
     * 五种运行模式（通过命令行参数选择）：
     *   V0.4 多进程模式：./mini_web_server conf/server.conf
     *   V0.5 多线程模式：./mini_web_server --thread conf/server.conf [num_workers]
     *   V0.6 TCP网络模式：./mini_web_server --tcp conf/server.conf
     *   V0.7 TCP多进程：  ./mini_web_server --fork conf/server.conf
     *   V0.8 TCP线程池：  ./mini_web_server --pool conf/server.conf [num_workers]
     *   V1.0 epoll模式：  ./mini_web_server serve-epoll <max_requests>
     *   V1.1 http模式：   ./mini_web_server serve-http <max_requests> (W3D1)
     *
     * V0.4 (process): fork 子进程，每个处理一个请求，通过 waitpid 回收
     * V0.5 (thread):  pthread 创建 worker 线程，共享请求队列，通过 pthread_join 回收
     * V0.6 (tcp):     socket/bind/listen/accept，单连接 TCP 服务器
     * V0.7 (fork):    socket/bind/listen/accept + fork，多进程并发 TCP 服务器
     * V0.8 (pool):    socket/bind/listen/accept + 线程池，固定 worker 处理连接
     * V1.0 (epoll):   socket/bind/listen + epoll_create1/epoll_ctl/epoll_wait 事件驱动
     * V1.1 (http):    epoll + HTTP 解析 + 路由分发 + 日志系统 (W3D1)
     */
    {
        int use_threads = 0;
        int use_tcp = 0;
        int use_fork = 0;
        int use_pool = 0;
        const char *config_path;
        int num_workers = 4;  /* 默认 4 个 worker 线程 */

        if (argc >= 3 && strcmp(argv[1], "--thread") == 0) {
            use_threads = 1;
            config_path = argv[2];
            if (argc >= 4) {
                num_workers = atoi(argv[3]);
                if (num_workers < 1) num_workers = 1;
            }
        } else if (argc >= 3 && strcmp(argv[1], "--tcp") == 0) {
            use_tcp = 1;
            config_path = argv[2];
        } else if (argc >= 3 && strcmp(argv[1], "--fork") == 0) {
            use_fork = 1;
            config_path = argv[2];
        } else if (argc >= 3 && strcmp(argv[1], "--pool") == 0) {
            use_pool = 1;
            config_path = argv[2];
            if (argc >= 4) {
                num_workers = atoi(argv[3]);
                if (num_workers < 1) num_workers = 1;
            }
        } else if (argc == 2) {
            config_path = argv[1];
        } else {
            print_usage(argv[0]);
            return 1;
        }

        server_config_t config;
        memset(&config, 0, sizeof(config));

        if (load_config(config_path, &config) != 0) {
            printf("failed to load config\n");
            return 1;
        }

        if (log_init(config.log_path, NULL) != 0) {
            fprintf(stderr, "failed to open log file\n");
            return 1;
        }

        log_info("server config loaded");
        log_info("document root loaded");
        print_config(&config);

        if (use_pool) {
            /* V0.8 线程池 TCP 网络服务器：固定数量 worker 线程并发处理客户端 */
            printf("Starting V0.8 thread-pool TCP server with %d workers...\n", num_workers);
            if (tcp_pool_server_run(&config, num_workers) < 0) {
                log_error("failed to start tcp pool server");
                log_close();
                return 1;
            }
        } else if (use_fork) {
            /* V0.7 多进程 TCP 网络服务器：fork 子进程并发处理多个客户端 */
            printf("Starting V0.7 multi-process TCP server...\n");
            if (tcp_fork_server_run(&config) < 0) {
                log_error("failed to start tcp fork server");
                log_close();
                return 1;
            }
        } else if (use_tcp) {
            /* V0.6 TCP 网络服务器：socket/bind/listen/accept 真实 TCP 连接 */
            printf("Starting V0.6 TCP server...\n");
            if (tcp_server_run(&config) < 0) {
                log_error("failed to start tcp server");
                log_close();
                return 1;
            }
        } else if (use_threads) {
            /* V0.5 多线程处理：主线程扫描 + worker 线程池处理 */
            printf("Starting V0.5 multi-threaded server with %d workers...\n", num_workers);
            if (thread_requests(num_workers) < 0) {
                log_error("failed to process requests (thread mode)");
                log_close();
                return 1;
            }
        } else {
            /* V0.4 多进程处理：父进程 fork 子进程处理每个请求 */
            printf("Starting V0.4 multi-process server...\n");
            if (process_requests() < 0) {
                log_error("failed to process requests (process mode)");
                log_close();
                return 1;
            }
        }

        log_close();
        return 0;
    }
}
