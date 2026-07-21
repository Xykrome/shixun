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
    printf("  V1.5 config-driven server:\n");
    printf("    %s <config.json> [max_requests]  - Start config-driven HTTP server (V1.5, W3D5)\n", prog);
    printf("\n");
    printf("  V1.x epoll servers:\n");
    printf("    %s serve-http <max_requests>    - Start epoll HTTP server (V1.3, W3D3)\n", prog);
    printf("    %s serve-epoll <max_requests>   - Start epoll HTTP server (V1.0, W2D5)\n", prog);
    printf("\n");
    printf("  Legacy V0.x servers:\n");
    printf("    %s <config_file>                - Start multi-process server (V0.4)\n", prog);
    printf("    %s --thread <config_file>       - Start multi-threaded server (V0.5)\n", prog);
    printf("    %s --thread <config_file> <N>   - Start multi-threaded server with N workers\n", prog);
    printf("    %s --tcp <config_file>          - Start TCP server, single connection (V0.6)\n", prog);
    printf("    %s --fork <config_file>         - Start TCP server, multi-process (V0.7)\n", prog);
    printf("    %s --pool <config_file>         - Start TCP server, thread pool (V0.8)\n", prog);
    printf("    %s --pool <config_file> <N>     - Start TCP server with N worker threads (V0.8)\n", prog);
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
     * 保留 V1.3 行为：路由硬编码为 /search(GET/POST) + /echo(POST)，
     * 内部转为 V1.5 的 config + route-table 机制运行。
     */
    if (argc >= 3 && strcmp(argv[1], "serve-http") == 0) {
        int max_requests = atoi(argv[2]);
        server_config_t cfg;

        if (max_requests <= 0) {
            fprintf(stderr, "Error: max_requests must be a positive integer\n");
            return 1;
        }

        /* 日志初始化：系统日志 + 访问日志分别写入 */
        if (log_init("logs/system.log", "logs/access.log") != 0) {
            fprintf(stderr, "Warning: failed to open log files, continuing without logging\n");
        }

        /* 构建默认配置（模拟 V1.3 行为） */
        memset(&cfg, 0, sizeof(cfg));
        strcpy(cfg.host, "0.0.0.0");
        cfg.port = 8080;
        strcpy(cfg.document_root, "www");
        strcpy(cfg.log_level, "INFO");
        strcpy(cfg.log_file, "logs/system.log");

        /* V1.3 路由 */
        strcpy(cfg.routes[0].method, "GET");
        strcpy(cfg.routes[0].path, "/search");
        strcpy(cfg.routes[0].handler, "search_get");
        strcpy(cfg.routes[1].method, "POST");
        strcpy(cfg.routes[1].path, "/search");
        strcpy(cfg.routes[1].handler, "search_post");
        strcpy(cfg.routes[2].method, "POST");
        strcpy(cfg.routes[2].path, "/echo");
        strcpy(cfg.routes[2].handler, "echo_post");
        cfg.route_count = 3;

        printf("Starting V1.3 epoll HTTP search server (max %d requests)...\n",
               max_requests);
        if (http_server_run(&cfg, max_requests) < 0) {
            log_error("failed to start http server");
            log_close();
            return 1;
        }

        log_close();
        return 0;
    }

    /* ===== Webserver V1.5 (W3D5): 配置驱动 HTTP 服务器 =====
     *
     * 命令格式：./mini_web_server <config.json> [max_requests]
     *
     * 从 JSON 配置文件读取 host/port/document_root/log/routes，
     * 校验后构建路由表并启动服务器。
     */
    if (argc >= 2 && strstr(argv[1], ".json") != NULL) {
        const char *config_path = argv[1];
        int max_requests = 0;  /* 0 = unlimited */
        server_config_t cfg;
        char access_log_path[320];

        if (argc >= 3) {
            max_requests = atoi(argv[2]);
            if (max_requests < 0) max_requests = 0;
        }

        memset(&cfg, 0, sizeof(cfg));

        /* 加载并校验配置 */
        if (load_json_config(config_path, &cfg) != 0) {
            fprintf(stderr, "FATAL: failed to load config from %s\n",
                    config_path);
            return 1;
        }
        print_config(&cfg);

        /* 日志初始化 — 访问日志从系统日志路径自动派生 */
        {
            const char *dot = strrchr(cfg.log_file, '.');
            if (dot) {
                size_t base_len = (size_t)(dot - cfg.log_file);
                if (base_len >= sizeof(access_log_path) - 12)
                    base_len = sizeof(access_log_path) - 12;
                memcpy(access_log_path, cfg.log_file, base_len);
                strcpy(access_log_path + base_len, "_access.log");
            } else {
                snprintf(access_log_path, sizeof(access_log_path),
                         "%s_access.log", cfg.log_file);
            }
        }

        if (log_init(cfg.log_file, access_log_path) != 0) {
            fprintf(stderr, "Warning: failed to open log files\n");
        }

        printf("\n=== W3D5 Config-Driven Server V1.5 ===\n");
        printf("Config : %s\n", config_path);
        printf("Host   : %s\n", cfg.host);
        printf("Port   : %d\n", cfg.port);
        printf("Root   : %s\n", cfg.document_root);
        printf("Log    : %s (level=%s)\n", cfg.log_file, cfg.log_level);
        printf("Routes : %d\n", cfg.route_count);
        printf("Max req: %d\n\n", max_requests);

        if (http_server_run(&cfg, max_requests) < 0) {
            log_error("failed to start V1.5 config-driven server");
            log_close();
            return 1;
        }

        log_close();
        return 0;
    }

    /* ===== Web服务器模式（V0.x 旧版 INI 配置） ===== */
    {
        int use_threads = 0;
        int use_tcp = 0;
        int use_fork = 0;
        int use_pool = 0;
        const char *config_path;
        int num_workers = 4;

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

        {
            server_config_t config;
            memset(&config, 0, sizeof(config));

            if (load_legacy_config(config_path, &config) != 0) {
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
                printf("Starting V0.8 thread-pool TCP server with %d workers...\n", num_workers);
                if (tcp_pool_server_run(&config, num_workers) < 0) {
                    log_error("failed to start tcp pool server");
                    log_close();
                    return 1;
                }
            } else if (use_fork) {
                printf("Starting V0.7 multi-process TCP server...\n");
                if (tcp_fork_server_run(&config) < 0) {
                    log_error("failed to start tcp fork server");
                    log_close();
                    return 1;
                }
            } else if (use_tcp) {
                printf("Starting V0.6 TCP server...\n");
                if (tcp_server_run(&config) < 0) {
                    log_error("failed to start tcp server");
                    log_close();
                    return 1;
                }
            } else if (use_threads) {
                printf("Starting V0.5 multi-threaded server with %d workers...\n", num_workers);
                if (thread_requests(num_workers) < 0) {
                    log_error("failed to process requests (thread mode)");
                    log_close();
                    return 1;
                }
            } else {
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
}
