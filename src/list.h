#include <stdint.h>
#include <string.h>
#include "redismodule.h"

typedef struct node {
    void *data;
    struct node *next;
} node_t;

typedef struct list {
    node_t *head, *tail;
    size_t len;
} list_t;

node_t *nodeNew(void *data);
void nodeFree(node_t *n);
list_t *listNew();
int listRemove(list_t *l, void *data);
void *listHeadPop(list_t *l);
void listHeadPush(list_t *l, void *data);
void listTailPush(list_t *l, void *data);
void listFree(list_t *l);