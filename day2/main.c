#include "list.h"

void clearInputBuffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

int main() {
    LinkList L;
    initList(&L);

    // 加载 CSV（若文件不存在则空链表）
    loadFromCSV(L, "users.csv");

    int choice;
    do {
        printf("\n========== 用户信息管理系统 ==========\n");
        printf("1. 显示所有用户\n");
        printf("2. 添加用户\n");
        printf("3. 删除用户\n");
        printf("4. 查找用户\n");
        printf("5. 修改用户信息\n");
        printf("6. 按用户名排序并显示\n");
        printf("7. 保存并退出\n");
        printf("请选择操作 (1-7): ");
        scanf("%d", &choice);
        clearInputBuffer(); 

        switch (choice) {
            case 1:
                printList(L);
                break;
            case 2: {
                User u;
                printf("请输入用户ID: ");
                scanf("%d", &u.id);
                clearInputBuffer();
                printf("请输入姓名: ");
                fgets(u.name, sizeof(u.name), stdin);
                u.name[strcspn(u.name, "\n")] = '\0';
                printf("请输入密码: ");
                fgets(u.password, sizeof(u.password), stdin);
                u.password[strcspn(u.password, "\n")] = '\0';
                printf("请输入联系方式: ");
                fgets(u.phone, sizeof(u.phone), stdin);
                u.phone[strcspn(u.phone, "\n")] = '\0';
                insertSorted(L, u);
                break;
            }
            case 3: {
                int id;
                printf("请输入要删除的用户ID: ");
                scanf("%d", &id);
                clearInputBuffer();
                if (deleteById(L, id)) {
                    printf("用户ID %d 删除成功。\n", id);
                } else {
                    printf("用户ID %d 不存在。\n", id);
                }
                break;
            }
            case 4: {
                int id;
                printf("请输入要查找的用户ID: ");
                scanf("%d", &id);
                clearInputBuffer();
                Node *p = findById(L, id);
                if (p) {
                    printf("找到用户：ID=%d, 姓名=%s, 密码=%s, 联系方式=%s\n",
                           p->data.id, p->data.name, p->data.password, p->data.phone);
                } else {
                    printf("用户ID %d 不存在。\n", id);
                }
                break;
            }
            case 5: {
                int oldId;
                printf("请输入要修改的用户ID: ");
                scanf("%d", &oldId);
                clearInputBuffer();
                Node *p = findById(L, oldId);
                if (!p) {
                    printf("用户ID %d 不存在。\n", oldId);
                    break;
                }
                User newData;
                printf("请输入新的用户信息:\n");
                printf("新ID: ");
                scanf("%d", &newData.id);
                clearInputBuffer();
                printf("新姓名: ");
                fgets(newData.name, sizeof(newData.name), stdin);
                newData.name[strcspn(newData.name, "\n")] = '\0';
                printf("新密码: ");
                fgets(newData.password, sizeof(newData.password), stdin);
                newData.password[strcspn(newData.password, "\n")] = '\0';
                printf("新联系方式: ");
                fgets(newData.phone, sizeof(newData.phone), stdin);
                newData.phone[strcspn(newData.phone, "\n")] = '\0';
                updateById(L, oldId, newData);
                break;
            }
            case 6:
                sortByName(L);
                printf("按姓名排序完成。\n");
                printList(L);
                break;
            case 7:
                saveToCSV(L, "users.csv");
                printf("感谢使用，再见！\n");
                break;
            default:
                printf("无效选择，请重试。\n");
        }
    } while (choice != 7);

    destroyList(L);
    return 0;
}