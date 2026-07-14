#include "user_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 内部函数：插入节点到二叉查找树 */
static user_index_node_t* insert_index_node(user_index_node_t *root, user_node_t *user) {
    if (root == NULL) {
        user_index_node_t *node = (user_index_node_t*)malloc(sizeof(user_index_node_t));
        node->user = user;
        node->left = NULL;
        node->right = NULL;
        return node;
    }
    int cmp = strcmp(user->username, root->user->username);
    if (cmp < 0) {
        root->left = insert_index_node(root->left, user);
    } else if (cmp > 0) {
        root->right = insert_index_node(root->right, user);
    } else {
        // 重名（理论上不会发生，因为用户名唯一），不做插入
        // 但为了安全，可忽略
    }
    return root;
}

int user_index_build(user_index_t *index, user_node_t *head) {
    if (index == NULL) return -1;
    index->root = NULL;
    index->size = 0;

    user_node_t *cur = head;
    while (cur) {
        index->root = insert_index_node(index->root, cur);
        index->size++;
        cur = cur->next;
    }
    return 0;
}

user_node_t* user_index_find(user_index_t *index, const char *username) {
    user_index_node_t *cur = index->root;
    while (cur) {
        int cmp = strcmp(username, cur->user->username);
        if (cmp == 0) return cur->user;
        else if (cmp < 0) cur = cur->left;
        else cur = cur->right;
    }
    return NULL;
}

/* 中序遍历（递归） */
static void inorder_recursive(user_index_node_t *node) {
    if (node == NULL) return;
    inorder_recursive(node->left);
    printf("%s\n", node->user->username);
    inorder_recursive(node->right);
}

void user_index_inorder(user_index_t *index) {
    if (index == NULL || index->root == NULL) {
        printf("索引为空\n");
        return;
    }
    inorder_recursive(index->root);
}

static void free_index_node(user_index_node_t *node) {
    if (node == NULL) return;
    free_index_node(node->left);
    free_index_node(node->right);
    free(node);
}

void user_index_free(user_index_t *index) {
    if (index == NULL) return;
    free_index_node(index->root);
    index->root = NULL;
    index->size = 0;
}

/* 比较查找过程 */
int user_index_compare(user_index_t *index, user_node_t *head, const char *username) {
    printf("----- 链表查找过程 -----\n");
    user_node_t *cur = head;
    int steps = 0;
    while (cur) {
        steps++;
        printf("步骤 %d: 比较 %s\n", steps, cur->username);
        if (strcmp(cur->username, username) == 0) {
            printf("链表查找成功，共比较 %d 次\n", steps);
            break;
        }
        cur = cur->next;
    }
    if (cur == NULL) {
        printf("链表查找失败，共比较 %d 次\n", steps);
    }

    printf("\n----- 索引查找过程 -----\n");
    user_index_node_t *node = index->root;
    steps = 0;
    while (node) {
        steps++;
        printf("步骤 %d: 比较 %s\n", steps, node->user->username);
        int cmp = strcmp(username, node->user->username);
        if (cmp == 0) {
            printf("索引查找成功，共比较 %d 次\n", steps);
            return 1;
        } else if (cmp < 0) {
            node = node->left;
        } else {
            node = node->right;
        }
    }
    printf("索引查找失败，共比较 %d 次\n", steps);
    return 0;
}