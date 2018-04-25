#include "list.h"

// A minimal linked list implementation (you gotta have one in every project!)
node_t *nodeNew(void *data) {
    node_t *n = RedisModule_Alloc(sizeof(node_t));
    n->data = data;
    n->next = NULL;
    return n;
}

void nodeFree(node_t *n) {
    if (n) {
        RedisModule_Free(n);
    }
}

list_t *listNew() {
    list_t *l = RedisModule_Alloc(sizeof(list_t));
    l->head = NULL;
    l->tail = NULL;
    l->len = 0;
    return l;
}

int listRemove(list_t *l, void *data) {
    node_t *prev = NULL, *curr = l->head;
    while (curr) {
        if (curr->data == data) {
            if (prev) {
                prev->next = curr->next;
            } else {
                l->head = curr->next;
            }
            if (!curr->next) {
                l->tail = prev;
            }
            l->len--;
            return 1;
        }
        prev = curr;
        curr = curr->next;
    }
    return 0;
}

void *listHeadPop(list_t *l) {
    void *data = NULL;
    if (l->len) {
        node_t *n = l->head;
        l->head = l->head->next;
        if (!l->head) {
            l->tail = NULL;
        }
        data = n->data;
        nodeFree(n);
        l->len--;
    }
    return data;
}

void listHeadPush(list_t *l, void *data) {
    node_t *n = nodeNew(data);
    if (l->head) {
        n->next = l->head;
    } else {    // empty list
        l->tail = n;
    }
    l->head = n;
    l->len++;
}

void listTailPush(list_t *l, void *data) {
    node_t *n = nodeNew(data);
    if (l->tail) {
        l->tail->next = n;
    } else { //empty list
        l->head = n;
    }
    l->tail = n;
    l->len++;
}

void listFree(list_t *l) {
    if (l) {
        while (l->len > 0) {
            listHeadPop(l);
        }
        RedisModule_Free(l);
    }
}