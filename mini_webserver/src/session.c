/*
 * W3D5 session.c — Session 会话认证实现 (V1.5 选做1)
 *
 * 功能：
 *   1. 随机 SessionID / CSRF Token 生成
 *   2. 内存会话存储（增删查改 + 过期清理）
 *   3. Cookie 解析与构建
 *
 * 安全规范：
 *   - 使用 /dev/urandom 生成不可预测的随机值
 *   - 登录后刷新 SessionID（防会话固定）
 *   - Cookie: HttpOnly（防 XSS）、SameSite=Strict（防 CSRF）
 */

#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

/* ===== 随机数生成辅助函数 ============================================== */

/*
 * 从 /dev/urandom 读取随机字节并转换为十六进制字符串。
 */
static int random_hex(char *buf, int hex_len)
{
    int fd;
    int raw_len = hex_len / 2;
    unsigned char raw[SESSION_ID_LEN / 2];
    ssize_t n;

    if (hex_len < 2 || hex_len % 2 != 0) return -1;

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;

    n = read(fd, raw, (size_t)raw_len);
    close(fd);

    if (n < raw_len) return -1;

    {
        int i;
        for (i = 0; i < raw_len; i++) {
            snprintf(buf + (i * 2), 3, "%02x", raw[i]);
        }
        buf[hex_len] = '\0';
    }

    return 0;
}

int session_generate_id(char *buf, int buf_size)
{
    if (!buf || buf_size < SESSION_ID_LEN + 1) return -1;
    return random_hex(buf, SESSION_ID_LEN);
}

int csrf_token_generate(char *buf, int buf_size)
{
    if (!buf || buf_size < CSRF_TOKEN_LEN + 1) return -1;
    return random_hex(buf, CSRF_TOKEN_LEN);
}

/* ===== 会话存储管理 ==================================================== */

void session_store_init(session_store_t *store)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));
}

session_t *session_create(session_store_t *store, const char *username)
{
    session_t *s;
    int i;

    if (!store || !username) return NULL;

    /* 先清理过期会话，释放 slot */
    session_cleanup_expired(store);

    /* 查找空闲 slot */
    for (i = 0; i < MAX_SESSIONS; i++) {
        if (!store->entries[i].active) break;
    }
    if (i >= MAX_SESSIONS) {
        /* 无可用 slot，强制清理最旧的过期会话后再试 */
        session_cleanup_expired(store);
        for (i = 0; i < MAX_SESSIONS; i++) {
            if (!store->entries[i].active) break;
        }
        if (i >= MAX_SESSIONS) return NULL;  /* 真的满了 */
    }

    s = &store->entries[i];
    memset(s, 0, sizeof(*s));

    /* 生成随机 SessionID 和 CSRF Token */
    if (session_generate_id(s->session_id, sizeof(s->session_id)) != 0) {
        return NULL;
    }
    if (csrf_token_generate(s->csrf_token, sizeof(s->csrf_token)) != 0) {
        memset(s->session_id, 0, sizeof(s->session_id));
        return NULL;
    }

    strncpy(s->username, username, SESSION_USER_LEN - 1);
    s->username[SESSION_USER_LEN - 1] = '\0';
    s->created_at  = time(NULL);
    s->last_access = s->created_at;
    s->active      = 1;

    if (store->count < MAX_SESSIONS) store->count++;

    return s;
}

session_t *session_lookup(session_store_t *store, const char *session_id)
{
    int i;
    time_t now;

    if (!store || !session_id) return NULL;

    now = time(NULL);

    for (i = 0; i < MAX_SESSIONS; i++) {
        session_t *s = &store->entries[i];
        if (!s->active) continue;
        if (strcmp(s->session_id, session_id) != 0) continue;

        /* 检查是否过期 */
        if (now - s->last_access > SESSION_TIMEOUT_SEC) {
            s->active = 0;
            if (store->count > 0) store->count--;
            return NULL;
        }

        /* 更新最后访问时间 */
        s->last_access = now;
        return s;
    }

    return NULL;
}

void session_destroy(session_store_t *store, const char *session_id)
{
    int i;

    if (!store || !session_id) return;

    for (i = 0; i < MAX_SESSIONS; i++) {
        session_t *s = &store->entries[i];
        if (!s->active) continue;
        if (strcmp(s->session_id, session_id) == 0) {
            s->active = 0;
            if (store->count > 0) store->count--;
            return;
        }
    }
}

void session_cleanup_expired(session_store_t *store)
{
    int i;
    time_t now;

    if (!store) return;

    now = time(NULL);

    for (i = 0; i < MAX_SESSIONS; i++) {
        session_t *s = &store->entries[i];
        if (!s->active) continue;
        if (now - s->last_access > SESSION_TIMEOUT_SEC) {
            s->active = 0;
            if (store->count > 0) store->count--;
        }
    }
}

/* ===== Cookie 解析与构建 =============================================== */

int parse_cookie(const char *cookie_header, const char *name,
                 char *value, int value_size)
{
    const char *p;
    int name_len;

    if (!cookie_header || !name || !value || value_size <= 0) return -1;
    value[0] = '\0';

    name_len = (int)strlen(name);
    p = cookie_header;

    while (*p) {
        /* 跳过前导空白 */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        /* 检查 cookie 名称是否匹配 */
        if (strncmp(p, name, (size_t)name_len) == 0 && p[name_len] == '=') {
            const char *val_start = p + name_len + 1;
            const char *val_end = strchr(val_start, ';');
            int val_len;

            if (val_end) {
                val_len = (int)(val_end - val_start);
            } else {
                val_len = (int)strlen(val_start);
            }

            if (val_len >= value_size) val_len = value_size - 1;
            memcpy(value, val_start, (size_t)val_len);
            value[val_len] = '\0';
            return 0;
        }

        /* 跳到下一个 cookie */
        p = strchr(p, ';');
        if (!p) break;
        p++;  /* 跳过 ';' */
    }

    return -1;
}

int build_set_cookie_header(const char *session_id, char *buf, int buf_size)
{
    if (!session_id || !buf || buf_size < COOKIE_MAX_LEN) return -1;

    return snprintf(buf, (size_t)buf_size,
                    "Set-Cookie: session_id=%s; HttpOnly; SameSite=Strict; Path=/",
                    session_id);
}

int build_clear_cookie_header(char *buf, int buf_size)
{
    if (!buf || buf_size < COOKIE_MAX_LEN) return -1;

    /* 最兼容的清除 Cookie 方式：空值 + Max-Age=0 + 过期 Expires */
    return snprintf(buf, (size_t)buf_size,
                    "Set-Cookie: session_id=; HttpOnly; SameSite=Strict; "
                    "Path=/; Max-Age=0; "
                    "Expires=Thu, 01 Jan 1970 00:00:00 GMT");
}
