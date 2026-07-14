#ifndef LIST_H
#define LIST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int id;
    char name[20];
    char password[20];
    char phone[15];
} User;

typedef struct Node {
    User data;
    struct Node *next;
} Node, *LinkList;

void initList(LinkList *L);
void destroyList(LinkList L);
void insertSorted(LinkList L, User u);          
int deleteById(LinkList L, int id);            
Node* findById(LinkList L, int id);            
void updateById(LinkList L, int id, User newData); 
void loadFromCSV(LinkList L, const char *filename);
void saveToCSV(LinkList L, const char *filename);
void sortByName(LinkList L);                    
void printList(LinkList L);

#endif