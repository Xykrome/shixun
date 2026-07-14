#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void remove_newline(char *text) {
    size_t len = strlen(text);
    if (len > 0 && (text[len-1] == '\n' || text[len-1] == '\r'))
        text[--len] = '\0';
    if (len > 0 && text[len-1] == '\r')
        text[--len] = '\0';
}

/* 去除字符串首尾空白字符（空格、制表符） */
static void trim(char *str) {
    char *end;
    while (*str == ' ' || *str == '\t') str++;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        end--;
    *(end + 1) = '\0';
}

int load_config(const char *path, server_config_t *config) {
    FILE *fp = fopen(path, "r");
    char line[256];
    if (fp == NULL || config == NULL) {
        return -1;
    }
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
        } else if (strcmp(key, "log") == 0) {
            strncpy(config->log_path, value, sizeof(config->log_path) - 1);
            config->log_path[sizeof(config->log_path) - 1] = '\0';
        }
    }
    fclose(fp);
    return 0;
}

void print_config(const server_config_t *config) {
    printf("server_name=%s\n", config->server_name);
    printf("host=%s\n", config->host);
    printf("port=%d\n", config->port);
    printf("root=%s\n", config->root);
    printf("log=%s\n", config->log_path);
}