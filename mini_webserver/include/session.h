/*
 * W3D5 session.h — Session 会话认证头文件 (V1.5 选做1)
 *
 * 功能：
 *   1. 不可预测的随机 SessionID 生成
 *   2. 服务端会话存储（内存哈希表）
 *   3. Cookie 解析与设置（HttpOnly, SameSite）
 *   4. CSRF Token 生成与校验
 *   5. 会话过期与清理
 *
 * 安全规范：
 *   - SessionID 使用 /dev/urandom 生成，不可预测
 *   - 登录后刷新 SessionID（防会话固定攻击）
 *   - Cookie 标记 HttpOnly（防 XSS 窃取）、SameSite=Strict（防 CSRF）
 *   - CSRF Token 独立校验（防御深度）
 */

#ifndef SESSION_H
#define SESSION_H

#include <time.h>

#define MAX_SESSIONS        64              /* 最大活跃会话数               */
#define SESSION_ID_LEN      64              /* SessionID 十六进制长度       */
#define SESSION_USER_LEN    64              /* 用户名字段最大长度           */
#define CSRF_TOKEN_LEN      32              /* CSRF Token 十六进制长度      */
#define SESSION_TIMEOUT_SEC 1800            /* 会话超时（30 分钟）          */
#define COOKIE_MAX_LEN      256             /* Cookie 头最大长度            */

/*
 * 单个会话条目
 */
typedef struct {
    char session_id[SESSION_ID_LEN + 1];    /* 随机 SessionID              */
    char username[SESSION_USER_LEN];        /* 已认证用户名                */
    char csrf_token[CSRF_TOKEN_LEN + 1];    /* CSRF 防护 Token             */
    time_t created_at;                      /* 创建时间戳                  */
    time_t last_access;                     /* 最后访问时间戳              */
    int    active;                          /* 1 = 活跃, 0 = 已销毁        */
} session_t;

/*
 * 会话存储
 */
typedef struct {
    session_t entries[MAX_SESSIONS];
    int       count;
} session_store_t;

/*
 * 初始化会话存储。
 * 必须在服务器启动时调用一次。
 */
void session_store_init(session_store_t *store);

/*
 * 使用 /dev/urandom 生成不可预测的随机 SessionID。
 * 输出为十六进制字符串，长度 SESSION_ID_LEN。
 */
int session_generate_id(char *buf, int buf_size);

/*
 * 生成 CSRF Token（16 随机字节 → 32 字符十六进制）。
 */
int csrf_token_generate(char *buf, int buf_size);

/*
 * 创建新会话。
 * 登录成功后调用，生成新 SessionID + CSRF Token，存入 store。
 * 返回 session 指针，失败返回 NULL。
 */
session_t *session_create(session_store_t *store, const char *username);

/*
 * 按 SessionID 查找有效会话。
 * 如果会话已过期，自动标记为非活跃并返回 NULL。
 * 返回 NULL 表示未找到或已过期。
 */
session_t *session_lookup(session_store_t *store, const char *session_id);

/*
 * 销毁会话（登出时调用）。
 * 将会话标记为非活跃。
 */
void session_destroy(session_store_t *store, const char *session_id);

/*
 * 清理所有过期会话。
 * 可定期调用（或在每次查找时懒清理）。
 */
void session_cleanup_expired(session_store_t *store);

/*
 * 从 HTTP 请求的 Cookie 头中提取指定名称的 Cookie 值。
 * 找到返回 0 并将值写入 value，未找到返回 -1。
 */
int parse_cookie(const char *cookie_header, const char *name,
                 char *value, int value_size);

/*
 * 构建 Set-Cookie 响应头。
 * 格式：session_id=<id>; HttpOnly; SameSite=Strict; Path=/
 */
int build_set_cookie_header(const char *session_id, char *buf, int buf_size);

/*
 * 构建清除 Cookie 的响应头（登出时使用）。
 * 格式：session_id=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0
 */
int build_clear_cookie_header(char *buf, int buf_size);

#endif /* SESSION_H */
