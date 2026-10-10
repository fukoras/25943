#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

typedef struct Node
{
    char *str;
    struct Node *next;
} Node;

void appendNode(Node **head, Node **tail, const char *str)
{
    size_t len = strlen(str);

    char *newStr = (char *)malloc(len + 1);
    if (newStr == NULL)
    {
        fprintf(stderr, "Malloc for string Error!\n");
        exit(1);
    }
    strcpy(newStr, str);

    Node *newNode = (Node *)malloc(sizeof(Node));
    if (newNode == NULL)
    {
        fprintf(stderr, "Malloc for Node error!\n");
        exit(1);
    }
    newNode->str = newStr;
    newNode->next = NULL;

    if (*head == NULL)
    {
        *head = newNode;
        *tail = newNode;
    }
    else
    {
        (*tail)->next = newNode;
        *tail = newNode;
    }
}

void printList(Node *head)
{
    Node *curr = head;
    while (curr != NULL)
    {
        printf("%s", curr->str);
        curr = curr->next;
    }
}

void freeList(Node *head)
{
    while (head != NULL)
    {
        Node *temp = head;
        head = head->next;
        free(temp->str);
        free(temp);
    }
}

int main(void)
{
    const int MAX_LEN = 1024;
    char buffer[MAX_LEN];

    Node *head = NULL;
    Node *tail = NULL;

    printf("Write string ('.' at start ends input): ");
    
    while(1)
    {
        
        if (fgets(buffer, MAX_LEN, stdin) == NULL)
            break;
        
        if (buffer[0] == '.')
            break;

        printf("Write string ('.' at start ends input): ");
        appendNode(&head, &tail, buffer);
    }

    printf("\nList contains:\n");
    printList(head);

    freeList(head);

    return 0;
}