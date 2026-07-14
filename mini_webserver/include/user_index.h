#ifndef USER_INDEX_H
#define USER_INDEX_H

#include "user_store.h"

/* 索引节点：保存指向 user_node_t 的指针，避免数据冗余 */
typedef struct user_index_node {
    user_node_t *user;                  // 指向链表中的用户数据
    struct user_index_node *left;
    struct user_index_node *right;
} user_index_node_t;

typedef struct {
    user_index_node_t *root;
    int size;                           // 索引节点数量
} user_index_t;

/* 从用户链表构建二叉查找树索引（按用户名升序） */
int user_index_build(user_index_t *index, user_node_t *head);

/* 在索引中查找用户名，返回指向用户的指针，未找到返回 NULL */
user_node_t* user_index_find(user_index_t *index, const char *username);

/* 中序遍历索引，按用户名升序输出（可输出到 stdout） */
void user_index_inorder(user_index_t *index);

/* 释放索引树（只释放索引节点，不释放 user_node_t） */
void user_index_free(user_index_t *index);

/* 比较链表和索引的查找过程：
 *   - 链表：从头节点开始，依次比较，输出比较节点名和比较次数
 *   - 索引：从根开始，输出路径节点名和比较次数
 * 返回 1 表示找到，0 表示未找到
 */
int user_index_compare(user_index_t *index, user_node_t *head, const char *username);

#endif