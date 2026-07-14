#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NAME  20
#define MAX_PHONE 20

typedef struct Contact {
    char name[MAX_NAME];
    char phone[MAX_PHONE];
} Contact;

typedef struct TreeNode {
    Contact contact;
    struct TreeNode *left;
    struct TreeNode *right;
} TreeNode;

// 创建新节点
TreeNode* createNode(Contact contact) {
    TreeNode *newNode = (TreeNode*)malloc(sizeof(TreeNode));
    if (!newNode) {
        perror("malloc");
        exit(1);
    }
    newNode->contact = contact;
    newNode->left = NULL;
    newNode->right = NULL;
    return newNode;
}

// 插入节点（按姓名升序，不允许重名）
TreeNode* insertNode(TreeNode *root, Contact contact) {
    if (root == NULL) {
        return createNode(contact);
    }

    int cmp = strcmp(contact.name, root->contact.name);
    if (cmp < 0) {
        root->left = insertNode(root->left, contact);
    } else if (cmp > 0) {
        root->right = insertNode(root->right, contact);
    } else {
        printf("联系人 \"%s\" 已存在，插入失败！\n", contact.name);
    }
    return root;
}

// 查找节点
TreeNode* searchNode(TreeNode *root, char *name) {
    if (root == NULL || strcmp(root->contact.name, name) == 0) {
        return root;
    }

    if (strcmp(name, root->contact.name) < 0) {
        return searchNode(root->left, name);
    } else {
        return searchNode(root->right, name);
    }
}

// 打印单个联系人信息
void printContact(TreeNode *node) {
    printf("姓名: %s, 电话: %s\n", node->contact.name, node->contact.phone);
}


TreeNode* deleteNode(TreeNode *root, char *name) {
    if (root == NULL) {
        printf("未找到要删除的联系人 \"%s\"\n", name);
        return NULL;
    }

    int cmp = strcmp(name, root->contact.name);

    if (cmp < 0) {
        root->left = deleteNode(root->left, name);
    } else if (cmp > 0) {
        root->right = deleteNode(root->right, name);
    } else {
        if (root->left == NULL) {
            TreeNode *temp = root->right;
            free(root);
            printf("联系人 \"%s\" 已删除。\n", name);
            return temp;
        }
        // 情况2：只有左子树
        else if (root->right == NULL) {
            TreeNode *temp = root->left;
            free(root);
            printf("联系人 \"%s\" 已删除。\n", name);
            return temp;
        }

        TreeNode *temp = root->right;
        while (temp->left != NULL) {
            temp = temp->left;
        }

        root->contact = temp->contact;

        root->right = deleteNode(root->right, temp->contact.name);
    }
    return root;
}

void modifyContact(TreeNode *root, char *name, char *newPhone) {
    TreeNode *node = searchNode(root, name);
    if (node != NULL) {
        strcpy(node->contact.phone, newPhone);
        printf("联系人 \"%s\" 的电话已修改为: %s\n", name, newPhone);
    } else {
        printf("未找到联系人 \"%s\"，修改失败！\n", name);
    }
}

void inorderTraversal(TreeNode *root) {
    if (root != NULL) {
        inorderTraversal(root->left);
        printContact(root);
        inorderTraversal(root->right);
    }
}

// 将树保存到CSV文件
void saveToFile(TreeNode *root, FILE *fp) {
    if (root != NULL) {
        saveToFile(root->left, fp);
        fprintf(fp, "%s,%s\n", root->contact.name, root->contact.phone);
        saveToFile(root->right, fp);
    }
}

// 释放整棵树
void freeTree(TreeNode *root) {
    if (root != NULL) {
        freeTree(root->left);
        freeTree(root->right);
        free(root);
    }
}

// ---------- 主菜单 ----------
int main() {
    TreeNode *root = NULL;

    // 1. 从 contacts.csv 读取联系人
    FILE *fp = fopen("contacts.csv", "r");
    if (fp == NULL) {
        printf("警告: contacts.csv 不存在，将新建空地址簿。\n");
    } else {
        char line[100];
        while (fgets(line, sizeof(line), fp)) {
            // 移除换行符
            line[strcspn(line, "\n")] = '\0';

            char *name = strtok(line, ",");
            char *phone = strtok(NULL, ",");
            if (name && phone) {
                Contact contact;
                strncpy(contact.name, name, MAX_NAME - 1);
                contact.name[MAX_NAME - 1] = '\0';
                strncpy(contact.phone, phone, MAX_PHONE - 1);
                contact.phone[MAX_PHONE - 1] = '\0';
                root = insertNode(root, contact);
            }
        }
        fclose(fp);
        printf("已从 contacts.csv 加载联系人。\n\n");
    }

    int choice;
    char name[MAX_NAME];
    char phone[MAX_PHONE];
    Contact newContact;

    do {
        printf("\n========== 地址簿管理系统 (训练二) ==========\n");
        printf("1. 显示所有联系人（按姓名升序）\n");
        printf("2. 查找联系人\n");
        printf("3. 新增联系人\n");
        printf("4. 修改联系人电话\n");
        printf("5. 删除联系人\n");
        printf("6. 保存到 contacts_result.csv 并退出\n");
        printf("请选择操作: ");
        scanf("%d", &choice);
        getchar(); // 吸收回车

        switch (choice) {
            case 1:
                if (root == NULL) {
                    printf("地址簿为空。\n");
                } else {
                    printf("\n所有联系人:\n");
                    inorderTraversal(root);
                }
                break;

            case 2:
                printf("请输入要查找的姓名: ");
                fgets(name, sizeof(name), stdin);
                name[strcspn(name, "\n")] = '\0';
                TreeNode *found = searchNode(root, name);
                if (found) {
                    printf("找到: ");
                    printContact(found);
                } else {
                    printf("未找到联系人 \"%s\"\n", name);
                }
                break;

            case 3:
                printf("请输入新增联系人姓名: ");
                fgets(name, sizeof(name), stdin);
                name[strcspn(name, "\n")] = '\0';
                if (searchNode(root, name) != NULL) {
                    printf("联系人 \"%s\" 已存在，不能重复新增。\n", name);
                } else {
                    printf("请输入电话: ");
                    fgets(phone, sizeof(phone), stdin);
                    phone[strcspn(phone, "\n")] = '\0';
                    strcpy(newContact.name, name);
                    strcpy(newContact.phone, phone);
                    root = insertNode(root, newContact);
                    printf("新增成功！\n");
                }
                break;

            case 4: // 修改（训练二任务）
                printf("请输入要修改的联系人姓名: ");
                fgets(name, sizeof(name), stdin);
                name[strcspn(name, "\n")] = '\0';
                printf("请输入新的电话号码: ");
                fgets(phone, sizeof(phone), stdin);
                phone[strcspn(phone, "\n")] = '\0';
                modifyContact(root, name, phone);
                break;

            case 5: // 删除（训练二任务）
                printf("请输入要删除的联系人姓名: ");
                fgets(name, sizeof(name), stdin);
                name[strcspn(name, "\n")] = '\0';
                root = deleteNode(root, name);
                break;

            case 6: // 保存并退出（训练二任务）
                fp = fopen("contacts_result.csv", "w");
                if (fp == NULL) {
                    printf("错误: 无法创建 contacts_result.csv 文件。\n");
                } else {
                    saveToFile(root, fp);
                    fclose(fp);
                    printf("联系人已成功保存到 contacts_result.csv\n");
                }
                printf("程序退出。\n");
                freeTree(root);
                return 0;

            default:
                printf("无效选项，请重新选择。\n");
        }
    } while (1);

    return 0;
}