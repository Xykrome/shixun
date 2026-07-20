/*
 * W3D5 bearer.h — Bearer Token 认证头文件 (V1.5 选做2)
 *
 * 功能：
 *   1. 不可预测的随机 Bearer Token 生成
 *   2. 服务端 Token 存储与校验
 *   3. Token 过期管理
 *   4. Authorization: Bearer <token> 头解析
 *
 * 安全规范：
 *   - Token 使用 /dev/urandom 生成，不可预测
 *   - Token 存储服务端状态（不透明 Token 模式）
 *   - 支持 exp 过期时间
 *   - 可扩展为 JWT（替换签名为 HMAC-SHA256）
 *   - 负载禁止存储密码（强制）
 *
 * 说明：
 *   当前实现为不透明 Token（服务端查表校验）。
 *   生产环境建议使用成熟 JWT 库（如 libjwt + OpenSSL）替换。
 */

#ifndef BEARER_H
#define BEARER_H

#include <time.h>

#define MAX_TOKENS          64              /* 最大活跃 Token 数            */
#define TOKEN_ID_LEN        64              /* Token 十六进制长度           */
#define TOKEN_USER_LEN      64              /* 用户名字段最大长度           */
#define TOKEN_TIMEOUT_SEC   3600            /* Token 超时（1 小时）         */

/*
 * Token 条目
 */
typedef struct {
    char    token_id[TOKEN_ID_LEN + 1];     /* 随机 Token 字符串           */
    char    username[TOKEN_USER_LEN];       /* 所属用户名                  */
    char    issuer[64];                     /* iss: 签发者                 */
    char    audience[64];                   /* aud: 受众                   */
    time_t  created_at;                     /* 创建时间戳                  */
    time_t  expires_at;                     /* 过期时间戳                  */
    int     active;                         /* 1 = 活跃, 0 = 已吊销        */
} token_entry_t;

/*
 * Token 存储
 */
typedef struct {
    token_entry_t entries[MAX_TOKENS];
    int           count;
} token_store_t;

/*
 * 初始化 Token 存储。
 */
void token_store_init(token_store_t *store);

/*
 * 生成不可预测的随机 Token（十六进制字符串）。
 */
int token_generate(char *buf, int buf_size);

/*
 * 创建新 Token。
 * 返回 token_entry 指针，失败返回 NULL。
 */
token_entry_t *token_create(token_store_t *store, const char *username,
                            const char *issuer, const char *audience,
                            int timeout_sec);

/*
 * 按 Token 字符串查找有效 Token。
 * 自动检查过期，过期返回 NULL。
 */
token_entry_t *token_lookup(token_store_t *store, const char *token_id);

/*
 * 吊销 Token（登出时调用）。
 */
void token_revoke(token_store_t *store, const char *token_id);

/*
 * 清理过期 Token。
 */
void token_cleanup_expired(token_store_t *store);

/*
 * 从 Authorization 头中提取 Bearer Token。
 * 输入："Bearer <token>" → 输出：<token>
 * 校验 scheme 必须为 Bearer（大小写不敏感）。
 * 成功返回 0，失败返回 -1。
 */
int parse_bearer_token(const char *auth_header, char *token_buf, int buf_size);

#endif /* BEARER_H */
