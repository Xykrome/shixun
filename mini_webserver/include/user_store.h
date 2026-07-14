#ifndef USER_STORE_H
#define USER_STORE_H

typedef struct user_node {
    char username[64];
    char password[64];
    char phone[64];
    struct user_node *next;
} user_node_t;

int load_users(const char *csv_path, user_node_t **head);

void free_users(user_node_t *head);

void list_users(user_node_t *head);

int find_user(user_node_t *head, const char *username);

int add_user(user_node_t **head, const char *username, const char *password, const char *phone);

int delete_user(user_node_t **head, const char *username);

int save_users(const char *csv_path, user_node_t *head);

#endif