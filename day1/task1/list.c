#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 预定义数据结构
typedef struct stuInfo {
    char stuName[10];
    int age;
} ElemType;

// 链表节点
typedef struct node {
    ElemType data;
    struct node *next;
} ListNode, *ListPtr;

// 函数声明
ListPtr CreateList();
void PrintList(ListPtr head);
void InsertList(ListPtr head);
void FreeList(ListPtr head);

int main() {
    ListPtr ListHead = NULL;
    char command;
    while (1) {
        printf("\n1 Create List\n");
        printf("2 Print List\n");
        printf("3 Insert Node (at tail)\n");
        printf("4 Quit\n");
        printf("Please choose: ");
        scanf(" %c", &command);
        getchar();

        switch(command) {
            case '1':
                ListHead = CreateList();
                break;
            case '2':
                PrintList(ListHead);
                break;
            case '3':
                InsertList(ListHead);
                break;
            case '4':
                FreeList(ListHead);
                printf("Bye!\n");
                return 0;
            default:
                printf("Invalid choice!\n");
        }
    }
    return 0;
}

ListPtr CreateList() {
    ListPtr head = NULL, tail = NULL;
    ElemType temp;
    printf("Enter student name (empty to finish): ");
    while (1) {
        fgets(temp.stuName, sizeof(temp.stuName), stdin);
        temp.stuName[strcspn(temp.stuName, "\n")] = '\0';
        if (strlen(temp.stuName) == 0) break;

        printf("Enter age: ");
        scanf("%d", &temp.age);
        getchar();

        ListPtr newNode = (ListPtr)malloc(sizeof(ListNode));
        newNode->data = temp;
        newNode->next = NULL;

        if (head == NULL) {
            head = tail = newNode;
        } else {
            tail->next = newNode;
            tail = newNode;
        }
        printf("Enter next student name (empty to finish): ");
    }
    return head;
}

void PrintList(ListPtr head) {
    if (head == NULL) {
        printf("List is empty.\n");
        return;
    }
    int count = 1;
    ListPtr p = head;
    while (p != NULL) {
        printf("Node %d: Name=%s, Age=%d\n", count++, p->data.stuName, p->data.age);
        p = p->next;
    }
}

void InsertList(ListPtr head) {
    if (head == NULL) {
        printf("List not created yet. Please create first.\n");
        return;
    }
    ElemType temp;
    printf("Enter new student name: ");
    fgets(temp.stuName, sizeof(temp.stuName), stdin);
    temp.stuName[strcspn(temp.stuName, "\n")] = '\0';
    printf("Enter age: ");
    scanf("%d", &temp.age);
    getchar();

    ListPtr newNode = (ListPtr)malloc(sizeof(ListNode));
    newNode->data = temp;
    newNode->next = NULL;

    ListPtr p = head;
    while (p->next != NULL) p = p->next;
    p->next = newNode;
    printf("Inserted successfully.\n");
}

void FreeList(ListPtr head) {
    ListPtr p = head;
    while (p != NULL) {
        ListPtr tmp = p;
        p = p->next;
        free(tmp);
    }
}