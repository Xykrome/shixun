#include "user_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 去除行尾的换行符和回车符 */
static void strip_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len-1] == '\n') str[--len] = '\0';
    if (len > 0 && str[len-1] == '\r') str[--len] = '\0';
}

int load_users(const char *csv_path, user_node_t **head) {
    FILE *fp = fopen(csv_path, "r");
    if (!fp) return -1;

    char line[256];
    int first_line = 1;  // 跳过标题行
    while (fgets(line, sizeof(line), fp)) {
        if (first_line) {
            first_line = 0;
            continue;
        }
        strip_newline(line);
        char *username = strtok(line, ",");
        if (!username) continue;
        char *password = strtok(NULL, ",");
        if (!password) continue;
        char *phone = strtok(NULL, ",");
        if (!phone) continue;

        user_node_t *node = (user_node_t*)malloc(sizeof(user_node_t));
        if (!node) {
            fclose(fp);
            return -1;
        }
        strncpy(node->username, username, sizeof(node->username) - 1);
        node->username[sizeof(node->username) - 1] = '\0';
        strncpy(node->password, password, sizeof(node->password) - 1);
        node->password[sizeof(node->password) - 1] = '\0';
        strncpy(node->phone, phone, sizeof(node->phone) - 1);
        node->phone[sizeof(node->phone) - 1] = '\0';
        node->next = *head;
        *head = node;
    }
    fclose(fp);
    return 0;
}

void free_users(user_node_t *head) {
    user_node_t *tmp;
    while (head) {
        tmp = head;
        head = head->next;
        free(tmp);
    }
}

void list_users(user_node_t *head) {
    user_node_t *cur = head;
    while (cur) {
        printf("%s,%s,%s\n", cur->username, cur->password, cur->phone);
        cur = cur->next;
    }
}

int find_user(user_node_t *head, const char *username) {
    user_node_t *cur = head;
    while (cur) {
        if (strcmp(cur->username, username) == 0)
            return 1;
        cur = cur->next;
    }
    return 0;
}

int add_user(user_node_t **head, const char *username, const char *password, const char *phone) {
    if (find_user(*head, username)) return -1;
    user_node_t *node = (user_node_t*)malloc(sizeof(user_node_t));
    if (!node) return -1;
    strncpy(node->username, username, sizeof(node->username) - 1);
    node->username[sizeof(node->username) - 1] = '\0';
    strncpy(node->password, password, sizeof(node->password) - 1);
    node->password[sizeof(node->password) - 1] = '\0';
    strncpy(node->phone, phone, sizeof(node->phone) - 1);
    node->phone[sizeof(node->phone) - 1] = '\0';
    node->next = *head;
    *head = node;
    return 0;
}

int delete_user(user_node_t **head, const char *username) {
    user_node_t *cur = *head;
    user_node_t *prev = NULL;
    while (cur) {
        if (strcmp(cur->username, username) == 0) {
            if (prev) prev->next = cur->next;
            else *head = cur->next;
            free(cur);
            return 0;
        }
        prev = cur;
        cur = cur->next;
    }
    return -1;
}

int save_users(const char *csv_path, user_node_t *head) {
    FILE *fp = fopen(csv_path, "w");
    if (!fp) return -1;

    // 写入标题行（保持与原始文件一致）
    fprintf(fp, "username,password,phone\n");

    user_node_t *cur = head;
    while (cur) {
        fprintf(fp, "%s,%s,%s\n", cur->username, cur->password, cur->phone);
        cur = cur->next;
    }

    fclose(fp);
    return 0;
}