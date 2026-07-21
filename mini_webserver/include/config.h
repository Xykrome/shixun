/*
 * W3D5 config.h — JSON-driven server configuration (V1.5)
 *
 * Loads server.json via json_parser, validates every field,
 * and exposes a structured config + route table for the HTTP server.
 *
 * Backward-compatible with V0.x legacy INI config via load_legacy_config().
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>   /* size_t */

#define MAX_ROUTES      32           /* max route entries in config        */
#define MAX_HOST_LEN    64           /* max host/IP string length          */
#define MAX_ROOT_LEN    256          /* max document_root path length      */
#define MAX_LOG_PATH    256          /* max log file path length           */
#define MAX_METHOD_LEN  16           /* GET / POST / ...                   */
#define MAX_RPATH_LEN   256          /* route path length                  */
#define MAX_HNAME_LEN   64           /* handler name length                */
#define MAX_AUTH_LEN    16           /* auth scheme ("", "basic")          */

/* ---- One route entry from config -------------------------------------- */
typedef struct {
    char method[MAX_METHOD_LEN];
    char path[MAX_RPATH_LEN];
    char handler[MAX_HNAME_LEN];
    char auth[MAX_AUTH_LEN];         /* "" = public, "basic" = Basic auth  */
} route_config_t;

/* ---- Complete server configuration ------------------------------------ */
typedef struct {
    /* legacy fields (V0.x INI config compat — DO NOT REMOVE) */
    char server_name[64];
    char host[MAX_HOST_LEN];
    int  port;
    char root[MAX_ROOT_LEN];          /* V0.x uses this */
    char log_path[MAX_LOG_PATH];      /* V0.x uses this */

    /* V1.5 JSON config fields */
    char document_root[MAX_ROOT_LEN];
    char log_file[MAX_LOG_PATH];
    char log_level[16];               /* DEBUG | INFO | WARN | ERROR */

    /* V1.5 auth config */
    char basic_username[64];          /* Basic auth username              */
    char basic_password[64];          /* Basic auth password              */

    /* V1.5 session config */
    int  session_enabled;             /* 1 = session auth enabled         */
    int  session_timeout_sec;         /* session expiry in seconds        */

    /* V1.5 bearer config */
    int  bearer_enabled;              /* 1 = bearer token auth enabled    */
    int  bearer_timeout_sec;          /* token expiry in seconds          */

    /* routes (V1.5+) */
    route_config_t routes[MAX_ROUTES];
    int            route_count;
} server_config_t;

/*
 * Load and validate V1.5 JSON configuration from path.
 * On success returns 0 and fills *config.
 * On failure prints the reason to stderr and returns -1.
 */
int load_json_config(const char *path, server_config_t *config);

/*
 * Load legacy INI-style config (V0.x backward compat).
 * Kept for process/thread/tcp/fork/pool server modes.
 */
int load_legacy_config(const char *path, server_config_t *config);

/* Print loaded config for verification. */
void print_config(const server_config_t *config);

/*
 * Check if a given auth scheme string is supported.
 * Currently supports: "basic" (and "" for public routes).
 * Returns 1 if valid, 0 otherwise.
 */
int is_valid_auth_scheme(const char *auth);

#endif /* CONFIG_H */
