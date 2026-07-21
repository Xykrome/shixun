/*
 * W3D5 config.c — JSON config loader + Legacy INI loader (V1.5)
 *
 * load_json_config():   parse server.json via cJSON, validate, populate config
 * load_legacy_config(): parse INI-style conf (V0.x backward compat)
 */

#include "config.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ===== Legacy INI config loader (V0.x backward compat) ================= */

static void remove_newline(char *text) {
    size_t len = strlen(text);
    if (len > 0 && (text[len-1] == '\n' || text[len-1] == '\r'))
        text[--len] = '\0';
    if (len > 0 && text[len-1] == '\r')
        text[--len] = '\0';
}

static void trim(char *str) {
    char *end;
    while (*str == ' ' || *str == '\t') str++;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        end--;
    *(end + 1) = '\0';
}

int load_legacy_config(const char *path, server_config_t *config) {
    FILE *fp = fopen(path, "r");
    char line[256];
    if (fp == NULL || config == NULL) {
        return -1;
    }
    memset(config, 0, sizeof(*config));

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *key;
        char *value;
        remove_newline(line);
        key = strtok(line, "=");
        if (key == NULL) continue;
        value = strtok(NULL, "=");
        if (value == NULL) continue;

        trim(key);
        trim(value);

        if (strcmp(key, "server_name") == 0) {
            strncpy(config->server_name, value, sizeof(config->server_name) - 1);
            config->server_name[sizeof(config->server_name) - 1] = '\0';
        } else if (strcmp(key, "host") == 0) {
            strncpy(config->host, value, sizeof(config->host) - 1);
            config->host[sizeof(config->host) - 1] = '\0';
        } else if (strcmp(key, "port") == 0) {
            config->port = atoi(value);
        } else if (strcmp(key, "root") == 0) {
            strncpy(config->root, value, sizeof(config->root) - 1);
            config->root[sizeof(config->root) - 1] = '\0';
            strncpy(config->document_root, value, sizeof(config->document_root) - 1);
            config->document_root[sizeof(config->document_root) - 1] = '\0';
        } else if (strcmp(key, "log") == 0) {
            strncpy(config->log_path, value, sizeof(config->log_path) - 1);
            config->log_path[sizeof(config->log_path) - 1] = '\0';
        }
    }
    fclose(fp);
    return 0;
}

/* ===== Handler registry (must match the names used in server.json) ===== */

/*
 * Handler function signature (V1.5):
 *   Return: total bytes sent, or -1 on error.
 */
typedef int (*Handler)(int client_fd, const void *req,
                       int *status_code, const char **mime_type,
                       int *body_bytes);

typedef struct {
    const char *name;
    Handler     fn;
} handler_entry_t;

/* Forward declarations of handler functions */
int search_get_handler(int client_fd, const void *req,
                       int *status_code, const char **mime_type,
                       int *body_bytes);
int search_post_handler(int client_fd, const void *req,
                        int *status_code, const char **mime_type,
                        int *body_bytes);
int echo_post_handler(int client_fd, const void *req,
                      int *status_code, const char **mime_type,
                      int *body_bytes);
int secured_get_handler(int client_fd, const void *req,
                        int *status_code, const char **mime_type,
                        int *body_bytes);
int serve_login_page(int client_fd, const void *req,
                     int *status_code, const char **mime_type,
                     int *body_bytes);
int session_login_handler(int client_fd, const void *req,
                          int *status_code, const char **mime_type,
                          int *body_bytes);
int session_logout_handler(int client_fd, const void *req,
                           int *status_code, const char **mime_type,
                           int *body_bytes);
int session_dashboard_handler(int client_fd, const void *req,
                              int *status_code, const char **mime_type,
                              int *body_bytes);
int token_post_handler(int client_fd, const void *req,
                       int *status_code, const char **mime_type,
                       int *body_bytes);
int api_me_handler(int client_fd, const void *req,
                   int *status_code, const char **mime_type,
                   int *body_bytes);

static const handler_entry_t HANDLER_REGISTRY[] = {
    {"search_get",         search_get_handler},
    {"search_post",        search_post_handler},
    {"echo_post",          echo_post_handler},
    {"secured_get",        secured_get_handler},
    {"serve_login_page",   serve_login_page},
    {"session_login",      session_login_handler},
    {"session_logout",     session_logout_handler},
    {"session_dashboard",  session_dashboard_handler},
    {"token_post",         token_post_handler},
    {"api_me",             api_me_handler},
};

#define REGISTRY_SIZE (sizeof(HANDLER_REGISTRY) / sizeof(HANDLER_REGISTRY[0]))

Handler find_handler_by_name(const char *name)
{
    size_t i;
    for (i = 0; i < REGISTRY_SIZE; i++) {
        if (strcmp(name, HANDLER_REGISTRY[i].name) == 0)
            return HANDLER_REGISTRY[i].fn;
    }
    return NULL;
}

/* ===== File I/O helpers ================================================ */

static char *read_entire_file(const char *path)
{
    FILE *fp;
    long  size;
    char *buf;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[CONFIG] cannot open file: %s\n", path);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0 || size > 10 * 1024 * 1024) {
        fprintf(stderr, "[CONFIG] file too large or size error: %s\n", path);
        fclose(fp);
        return NULL;
    }

    buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        fprintf(stderr, "[CONFIG] read error: %s\n", path);
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[size] = '\0';
    fclose(fp);
    return buf;
}

/* ===== Validation helpers ============================================== */

static int is_valid_port(int port)
{
    return port >= 1 && port <= 65535;
}

static int is_valid_log_level(const char *level)
{
    if (!level) return 0;
    return (strcmp(level, "DEBUG") == 0 ||
            strcmp(level, "INFO")  == 0 ||
            strcmp(level, "WARN")  == 0 ||
            strcmp(level, "ERROR") == 0);
}

static int is_valid_method(const char *method)
{
    if (!method) return 0;
    return (strcmp(method, "GET")    == 0 ||
            strcmp(method, "POST")   == 0 ||
            strcmp(method, "PUT")    == 0 ||
            strcmp(method, "DELETE") == 0);
}

static int is_valid_handler_name(const char *name)
{
    return name && name[0] != '\0' && find_handler_by_name(name) != NULL;
}

int is_valid_auth_scheme(const char *auth)
{
    if (!auth || auth[0] == '\0') return 1;  /* 空 = 公开路由，合法 */
    return (strcmp(auth, "basic") == 0 ||
            strcmp(auth, "session") == 0 ||
            strcmp(auth, "bearer") == 0);      /* V1.5: basic + session + bearer */
}

static int directory_exists(const char *path)
{
    struct stat st;
    if (!path || path[0] == '\0') return 0;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

/* ===== Main config loader (V1.5 — cJSON-based) ========================= */

int load_json_config(const char *path, server_config_t *config)
{
    char *raw;
    cJSON *root, *server, *logging, *routes;
    int i;

    if (!path || !config) return -1;

    memset(config, 0, sizeof(*config));

    /* ---- 1. Read and parse ---- */
    raw = read_entire_file(path);
    if (!raw) {
        fprintf(stderr, "[CONFIG] failed to read config file: %s\n", path);
        return -1;
    }

    root = cJSON_Parse(raw);
    free(raw);

    if (!root) {
        const char *err = cJSON_GetErrorPtr();
        fprintf(stderr, "[CONFIG] JSON parse error in %s: %s\n",
                path, err ? err : "unknown error");
        return -1;
    }

    if (!cJSON_IsObject(root)) {
        fprintf(stderr, "[CONFIG] top-level must be a JSON object\n");
        cJSON_Delete(root);
        return -1;
    }

    /* ---- 2. Validate "server" section ---- */
    server = cJSON_GetObjectItem(root, "server");
    if (!server || !cJSON_IsObject(server)) {
        fprintf(stderr, "[CONFIG] missing or invalid \"server\" section\n");
        cJSON_Delete(root);
        return -1;
    }

    {
        cJSON *v;
        const char *s;

        /* host */
        v = cJSON_GetObjectItem(server, "host");
        if (!v || !cJSON_IsString(v) || !v->valuestring || v->valuestring[0] == '\0') {
            fprintf(stderr, "[CONFIG] server.host must be a non-empty string\n");
            cJSON_Delete(root); return -1;
        }
        strncpy(config->host, v->valuestring, MAX_HOST_LEN - 1);
        config->host[MAX_HOST_LEN - 1] = '\0';

        /* port */
        v = cJSON_GetObjectItem(server, "port");
        if (!v || !cJSON_IsNumber(v)) {
            fprintf(stderr, "[CONFIG] server.port must be a number\n");
            cJSON_Delete(root); return -1;
        }
        config->port = v->valueint;
        if (!is_valid_port(config->port)) {
            fprintf(stderr, "[CONFIG] server.port=%d out of range (1–65535)\n",
                    config->port);
            cJSON_Delete(root); return -1;
        }

        /* document_root */
        v = cJSON_GetObjectItem(server, "document_root");
        s = (v && cJSON_IsString(v)) ? v->valuestring : NULL;
        if (!s || s[0] == '\0') {
            fprintf(stderr, "[CONFIG] server.document_root must be a non-empty string\n");
            cJSON_Delete(root); return -1;
        }
        strncpy(config->document_root, s, MAX_ROOT_LEN - 1);
        config->document_root[MAX_ROOT_LEN - 1] = '\0';
        if (!directory_exists(config->document_root)) {
            fprintf(stderr, "[CONFIG] server.document_root=\"%s\" does not exist "
                    "or is not a directory\n", config->document_root);
            cJSON_Delete(root); return -1;
        }
    }

    /* ---- 3. Validate "logging" section ---- */
    logging = cJSON_GetObjectItem(root, "logging");
    if (!logging || !cJSON_IsObject(logging)) {
        fprintf(stderr, "[CONFIG] missing or invalid \"logging\" section\n");
        cJSON_Delete(root);
        return -1;
    }

    {
        cJSON *v;
        const char *s;

        /* level */
        v = cJSON_GetObjectItem(logging, "level");
        s = (v && cJSON_IsString(v)) ? v->valuestring : NULL;
        if (!s || !is_valid_log_level(s)) {
            fprintf(stderr, "[CONFIG] logging.level must be DEBUG|INFO|WARN|ERROR\n");
            cJSON_Delete(root); return -1;
        }
        strncpy(config->log_level, s, sizeof(config->log_level) - 1);
        config->log_level[sizeof(config->log_level) - 1] = '\0';

        /* file */
        v = cJSON_GetObjectItem(logging, "file");
        s = (v && cJSON_IsString(v)) ? v->valuestring : NULL;
        if (!s || s[0] == '\0') {
            fprintf(stderr, "[CONFIG] logging.file must be a non-empty string\n");
            cJSON_Delete(root); return -1;
        }
        strncpy(config->log_file, s, MAX_LOG_PATH - 1);
        config->log_file[MAX_LOG_PATH - 1] = '\0';
    }

    /* ---- 3.5 Validate optional "auth" section (V1.5) ---- */
    {
        cJSON *auth_section = cJSON_GetObjectItem(root, "auth");
        if (auth_section && cJSON_IsObject(auth_section)) {
            cJSON *basic = cJSON_GetObjectItem(auth_section, "basic");
            if (basic && cJSON_IsObject(basic)) {
                cJSON *v;
                const char *s;

                /* username */
                v = cJSON_GetObjectItem(basic, "username");
                s = (v && cJSON_IsString(v)) ? v->valuestring : NULL;
                if (!s || s[0] == '\0') {
                    fprintf(stderr, "[CONFIG] auth.basic.username must be a non-empty string\n");
                    cJSON_Delete(root); return -1;
                }
                strncpy(config->basic_username, s, sizeof(config->basic_username) - 1);
                config->basic_username[sizeof(config->basic_username) - 1] = '\0';

                /* password */
                v = cJSON_GetObjectItem(basic, "password");
                s = (v && cJSON_IsString(v)) ? v->valuestring : NULL;
                if (!s || s[0] == '\0') {
                    fprintf(stderr, "[CONFIG] auth.basic.password must be a non-empty string\n");
                    cJSON_Delete(root); return -1;
                }
                strncpy(config->basic_password, s, sizeof(config->basic_password) - 1);
                config->basic_password[sizeof(config->basic_password) - 1] = '\0';
            }
        }
        /* auth section is optional — no credentials = Basic auth won't work,
         * but server can still serve public routes */
    }

    /* ---- 3.6 Validate optional "session" section (V1.5 选做1) ---- */
    {
        cJSON *session_section = cJSON_GetObjectItem(root, "session");
        if (session_section && cJSON_IsObject(session_section)) {
            cJSON *v;

            v = cJSON_GetObjectItem(session_section, "enabled");
            if (v && cJSON_IsBool(v)) {
                config->session_enabled = cJSON_IsTrue(v) ? 1 : 0;
            } else {
                config->session_enabled = 0;  /* 默认关闭 */
            }

            v = cJSON_GetObjectItem(session_section, "timeout_sec");
            if (v && cJSON_IsNumber(v) && v->valueint > 0) {
                config->session_timeout_sec = v->valueint;
            } else {
                config->session_timeout_sec = 1800;  /* 30 分钟默认 */
            }
        } else {
            config->session_enabled = 0;
            config->session_timeout_sec = 1800;
        }
    }

    /* ---- 3.7 Validate optional "bearer" section (V1.5 选做2) ---- */
    {
        cJSON *bearer_section = cJSON_GetObjectItem(root, "bearer");
        if (bearer_section && cJSON_IsObject(bearer_section)) {
            cJSON *v;

            v = cJSON_GetObjectItem(bearer_section, "enabled");
            if (v && cJSON_IsBool(v)) {
                config->bearer_enabled = cJSON_IsTrue(v) ? 1 : 0;
            } else {
                config->bearer_enabled = 0;
            }

            v = cJSON_GetObjectItem(bearer_section, "timeout_sec");
            if (v && cJSON_IsNumber(v) && v->valueint > 0) {
                config->bearer_timeout_sec = v->valueint;
            } else {
                config->bearer_timeout_sec = 3600;  /* 1 小时默认 */
            }
        } else {
            config->bearer_enabled = 0;
            config->bearer_timeout_sec = 3600;
        }
    }

    /* ---- 4. Validate "routes" section ---- */
    routes = cJSON_GetObjectItem(root, "routes");
    if (!routes || !cJSON_IsArray(routes)) {
        fprintf(stderr, "[CONFIG] missing or invalid \"routes\" array\n");
        cJSON_Delete(root);
        return -1;
    }

    {
        int count = cJSON_GetArraySize(routes);
        if (count > MAX_ROUTES) {
            fprintf(stderr, "[CONFIG] too many routes (%d), max %d\n",
                    count, MAX_ROUTES);
            cJSON_Delete(root); return -1;
        }

        for (i = 0; i < count; i++) {
            cJSON *entry = cJSON_GetArrayItem(routes, i);
            cJSON *v;
            const char *s;
            int j;

            if (!entry || !cJSON_IsObject(entry)) {
                fprintf(stderr, "[CONFIG] routes[%d] must be an object\n", i);
                cJSON_Delete(root); return -1;
            }

            /* method */
            v = cJSON_GetObjectItem(entry, "method");
            s = (v && cJSON_IsString(v)) ? v->valuestring : NULL;
            if (!s || !is_valid_method(s)) {
                fprintf(stderr, "[CONFIG] routes[%d].method must be GET|POST|PUT|DELETE\n", i);
                cJSON_Delete(root); return -1;
            }
            strncpy(config->routes[i].method, s, MAX_METHOD_LEN - 1);
            config->routes[i].method[MAX_METHOD_LEN - 1] = '\0';

            /* path */
            v = cJSON_GetObjectItem(entry, "path");
            s = (v && cJSON_IsString(v)) ? v->valuestring : NULL;
            if (!s || s[0] != '/') {
                fprintf(stderr, "[CONFIG] routes[%d].path must start with '/'\n", i);
                cJSON_Delete(root); return -1;
            }
            strncpy(config->routes[i].path, s, MAX_RPATH_LEN - 1);
            config->routes[i].path[MAX_RPATH_LEN - 1] = '\0';

            /* handler */
            v = cJSON_GetObjectItem(entry, "handler");
            s = (v && cJSON_IsString(v)) ? v->valuestring : NULL;
            if (!s || !is_valid_handler_name(s)) {
                fprintf(stderr, "[CONFIG] routes[%d].handler=\"%s\" is not a registered handler\n",
                        i, s ? s : "(null)");
                cJSON_Delete(root); return -1;
            }
            strncpy(config->routes[i].handler, s, MAX_HNAME_LEN - 1);
            config->routes[i].handler[MAX_HNAME_LEN - 1] = '\0';

            /* auth (V1.5, optional — defaults to "" = public) */
            v = cJSON_GetObjectItem(entry, "auth");
            if (v && cJSON_IsString(v) && v->valuestring) {
                if (!is_valid_auth_scheme(v->valuestring)) {
                    fprintf(stderr, "[CONFIG] routes[%d].auth=\"%s\" is not a supported "
                            "auth scheme (only \"basic\" or empty)\n",
                            i, v->valuestring);
                    cJSON_Delete(root); return -1;
                }
                strncpy(config->routes[i].auth, v->valuestring, MAX_AUTH_LEN - 1);
                config->routes[i].auth[MAX_AUTH_LEN - 1] = '\0';
            } else {
                config->routes[i].auth[0] = '\0';  /* 默认：公开路由 */
            }

            /* uniqueness check: no duplicate (method, path) pairs */
            for (j = 0; j < i; j++) {
                if (strcmp(config->routes[i].method, config->routes[j].method) == 0 &&
                    strcmp(config->routes[i].path, config->routes[j].path) == 0) {
                    fprintf(stderr, "[CONFIG] duplicate route: %s %s (routes[%d] and routes[%d])\n",
                            config->routes[i].method, config->routes[i].path, j, i);
                    cJSON_Delete(root); return -1;
                }
            }
        }

        config->route_count = count;
    }

    /* ---- 5. Backward-compat: also fill legacy fields ---- */
    strncpy(config->root, config->document_root, sizeof(config->root) - 1);
    config->root[sizeof(config->root) - 1] = '\0';
    strncpy(config->log_path, config->log_file, sizeof(config->log_path) - 1);
    config->log_path[sizeof(config->log_path) - 1] = '\0';
    config->server_name[0] = '\0';

    cJSON_Delete(root);
    return 0;
}

/* ===== Print config ==================================================== */

void print_config(const server_config_t *config)
{
    int i;

    if (!config) return;

    printf("[CONFIG] server.host          = %s\n", config->host);
    printf("[CONFIG] server.port          = %d\n", config->port);
    printf("[CONFIG] server.document_root = %s\n", config->document_root);
    printf("[CONFIG] logging.level        = %s\n",
           config->log_level[0] ? config->log_level : "(not set)");
    printf("[CONFIG] logging.file         = %s\n", config->log_file);
    if (config->basic_username[0]) {
        printf("[CONFIG] auth.basic.username  = %s\n", config->basic_username);
        printf("[CONFIG] auth.basic.password  = ****\n");  /* 不打印密码 */
    }
    if (config->session_enabled) {
        printf("[CONFIG] session.enabled      = true\n");
        printf("[CONFIG] session.timeout_sec  = %d\n", config->session_timeout_sec);
    }
    if (config->bearer_enabled) {
        printf("[CONFIG] bearer.enabled       = true\n");
        printf("[CONFIG] bearer.timeout_sec   = %d\n", config->bearer_timeout_sec);
    }
    printf("[CONFIG] routes               = %d\n", config->route_count);

    for (i = 0; i < config->route_count; i++) {
        printf("[CONFIG]   [%d] %-6s %-20s -> %s",
               i, config->routes[i].method, config->routes[i].path,
               config->routes[i].handler);
        if (config->routes[i].auth[0]) {
            printf("  (auth:%s)", config->routes[i].auth);
        }
        printf("\n");
    }
}
