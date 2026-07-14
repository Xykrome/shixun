#include "list.h"

// 初始化带头结点的空链表
void initList(LinkList *L) {
    *L = (LinkList)malloc(sizeof(Node));
    if (!(*L)) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    (*L)->next = NULL;
}

// 销毁链表，释放所有结点
void destroyList(LinkList L) {
    Node *p;
    while (L) {
        p = L;
        L = L->next;
        free(p);
    }
}

// 按 id 递增插入结点
void insertSorted(LinkList L, User u) {
    Node *pre = L;
    Node *cur = L->next;
    while (cur && cur->data.id < u.id) {
        pre = cur;
        cur = cur->next;
    }
    if (cur && cur->data.id == u.id) {
        printf("用户 ID %d 已存在，插入失败。\n", u.id);
        return;
    }
    Node *s = (Node*)malloc(sizeof(Node));
    if (!s) {
        perror("malloc failed");
        return;
    }
    s->data = u;
    s->next = cur;
    pre->next = s;
}

// 按id删除结点
int deleteById(LinkList L, int id) {
    Node *pre = L;
    Node *cur = L->next;
    while (cur && cur->data.id != id) {
        pre = cur;
        cur = cur->next;
    }
    if (!cur) return 0; 
    pre->next = cur->next;
    free(cur);
    return 1;
}

Node* findById(LinkList L, int id) {
    Node *p = L->next;
    while (p && p->data.id != id) {
        p = p->next;
    }
    return p;
}

// 更新用户信息，删除旧结点再插入新结点
void updateById(LinkList L, int id, User newData) {
    Node *p = findById(L, id);
    if (!p) {
        printf("用户 ID %d 不存在，更新失败。\n", id);
        return;
    }
    if (newData.id != id) {
        if (findById(L, newData.id)) {
            printf("新 ID %d 已被占用，更新失败。\n", newData.id);
            return;
        }
        deleteById(L, id);
        insertSorted(L, newData);
        printf("用户 ID 已从 %d 更改为 %d。\n", id, newData.id);
    } else {
        p->data = newData;
        printf("用户 ID %d 信息已更新。\n", id);
    }
}

// 从 CSV 文件加载用户数据
void loadFromCSV(LinkList L, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("文件 %s 不存在，将创建新文件。\n", filename);
        return;
    }
    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return;
    }
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        User u;
        if (sscanf(line, "%d,%19[^,],%19[^,],%14[^,]", 
                   &u.id, u.name, u.password, u.phone) == 4) {
            insertSorted(L, u);
        } else {
            printf("解析行失败: %s\n", line);
        }
    }
    fclose(fp);
    printf("从 %s 加载数据完成。\n", filename);
}

// 保存链表数据到CSV文件
void saveToCSV(LinkList L, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("无法打开文件写入");
        return;
    }
    fprintf(fp, "id,name,password,phone\n");
    Node *p = L->next;
    while (p) {
        fprintf(fp, "%d,%s,%s,%s\n", p->data.id, p->data.name, p->data.password, p->data.phone);
        p = p->next;
    }
    fclose(fp);
    printf("数据已保存到 %s。\n", filename);
}

// 按用户名递增排序
void sortByName(LinkList L) {
    if (!L->next || !L->next->next) return; // 空或只有一个节点

    int swapped;
    Node *p;
    Node *last = NULL;
    do {
        swapped = 0;
        p = L->next;
        while (p->next != last) {
            if (strcmp(p->data.name, p->next->data.name) > 0) {
                // 交换数据域
                User temp = p->data;
                p->data = p->next->data;
                p->next->data = temp;
                swapped = 1;
            }
            p = p->next;
        }
        last = p;
    } while (swapped);
}

// 打印所有用户信息
void printList(LinkList L) {
    Node *p = L->next;
    if (!p) {
        printf("链表为空。\n");
        return;
    }
    printf("\n%-6s %-12s %-12s %-15s\n", "ID", "Name", "Password", "Phone");
    printf("-------------------------------------------------\n");
    while (p) {
        printf("%-6d %-12s %-12s %-15s\n", 
               p->data.id, p->data.name, p->data.password, p->data.phone);
        p = p->next;
    }
    printf("\n");
}