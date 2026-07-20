/*
 * W3D5 bearer.c — Bearer Token 认证实现 (V1.5 选做2)
 *
 * 功能：
 *   1. 随机 Token 生成（/dev/urandom）
 *   2. Token 存储与生命周期管理
 *   3. Authorization: Bearer <token> 解析
 *
 * 安全规范：
 *   - Token 使用 /dev/urandom 生成
 *   - 服务端查表校验（不透明 Token）
 *   - 支持过期时间（exp）
 *   - 支持签发者（iss）和受众（aud）校验
 */

#include "bearer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

/* ===== 随机 Token 生成 ================================================== */

int token_generate(char *buf, int buf_size)
{
    int fd;
    int raw_len;
    unsigned char raw[TOKEN_ID_LEN / 2];
    ssize_t n;
    int i;

    if (!buf || buf_size < TOKEN_ID_LEN + 1) return -1;
    raw_len = TOKEN_ID_LEN / 2;

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;

    n = read(fd, raw, (size_t)raw_len);
    close(fd);

    if (n < raw_len) return -1;

    for (i = 0; i < raw_len; i++) {
        snprintf(buf + (i * 2), 3, "%02x", raw[i]);
    }
    buf[TOKEN_ID_LEN] = '\0';

    return 0;
}

/* ===== Token 存储管理 =================================================== */

void token_store_init(token_store_t *store)
{
    if (!store) return;
    memset(store, 0, sizeof(*store));
}

token_entry_t *token_create(token_store_t *store, const char *username,
                            const char *issuer, const char *audience,
                            int timeout_sec)
{
    token_entry_t *t;
    int i;

    if (!store || !username) return NULL;

    if (timeout_sec <= 0) timeout_sec = TOKEN_TIMEOUT_SEC;

    /* 清理过期 Token */
    token_cleanup_expired(store);

    /* 查找空闲 slot */
    for (i = 0; i < MAX_TOKENS; i++) {
        if (!store->entries[i].active) break;
    }
    if (i >= MAX_TOKENS) {
        token_cleanup_expired(store);
        for (i = 0; i < MAX_TOKENS; i++) {
            if (!store->entries[i].active) break;
        }
        if (i >= MAX_TOKENS) return NULL;
    }

    t = &store->entries[i];
    memset(t, 0, sizeof(*t));

    if (token_generate(t->token_id, sizeof(t->token_id)) != 0) {
        return NULL;
    }

    strncpy(t->username, username, TOKEN_USER_LEN - 1);
    t->username[TOKEN_USER_LEN - 1] = '\0';

    if (issuer) {
        strncpy(t->issuer, issuer, sizeof(t->issuer) - 1);
        t->issuer[sizeof(t->issuer) - 1] = '\0';
    } else {
        strcpy(t->issuer, "mini_webserver");
    }

    if (audience) {
        strncpy(t->audience, audience, sizeof(t->audience) - 1);
        t->audience[sizeof(t->audience) - 1] = '\0';
    } else {
        strcpy(t->audience, "api");
    }

    t->created_at  = time(NULL);
    t->expires_at  = t->created_at + timeout_sec;
    t->active      = 1;

    if (store->count < MAX_TOKENS) store->count++;

    return t;
}

token_entry_t *token_lookup(token_store_t *store, const char *token_id)
{
    int i;
    time_t now;

    if (!store || !token_id) return NULL;

    now = time(NULL);

    for (i = 0; i < MAX_TOKENS; i++) {
        token_entry_t *t = &store->entries[i];
        if (!t->active) continue;
        if (strcmp(t->token_id, token_id) != 0) continue;

        /* 检查过期 */
        if (now > t->expires_at) {
            t->active = 0;
            if (store->count > 0) store->count--;
            return NULL;
        }

        return t;
    }

    return NULL;
}

void token_revoke(token_store_t *store, const char *token_id)
{
    int i;

    if (!store || !token_id) return;

    for (i = 0; i < MAX_TOKENS; i++) {
        token_entry_t *t = &store->entries[i];
        if (!t->active) continue;
        if (strcmp(t->token_id, token_id) == 0) {
            t->active = 0;
            if (store->count > 0) store->count--;
            return;
        }
    }
}

void token_cleanup_expired(token_store_t *store)
{
    int i;
    time_t now;

    if (!store) return;

    now = time(NULL);

    for (i = 0; i < MAX_TOKENS; i++) {
        token_entry_t *t = &store->entries[i];
        if (!t->active) continue;
        if (now > t->expires_at) {
            t->active = 0;
            if (store->count > 0) store->count--;
        }
    }
}

/* ===== Authorization 头解析 ============================================= */

int parse_bearer_token(const char *auth_header, char *token_buf, int buf_size)
{
    const char *space;
    const char *token_start;
    int token_len;

    if (!auth_header || !token_buf || buf_size <= 0) return -1;
    token_buf[0] = '\0';

    /* 查找空格分隔 scheme 和 token */
    space = strchr(auth_header, ' ');
    if (!space) return -1;

    /* 校验 scheme */
    {
        int scheme_len = (int)(space - auth_header);
        if (scheme_len != 6 || strncasecmp(auth_header, "Bearer", 6) != 0) {
            return -1;  /* scheme 不是 Bearer */
        }
    }

    /* 跳过空白 */
    token_start = space + 1;
    while (*token_start == ' ' || *token_start == '\t') token_start++;

    if (*token_start == '\0') return -1;

    /* 复制 token */
    token_len = (int)strlen(token_start);
    if (token_len >= buf_size) token_len = buf_size - 1;
    memcpy(token_buf, token_start, (size_t)token_len);
    token_buf[token_len] = '\0';

    return 0;
}
