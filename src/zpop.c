/* The Z module!
 * It implements the top requested Redis feature request: ZPOP!
 * It also does a blocking ZPOP!
 * It is mainly for fun and educational purposes, but feel free to use it for anything else ;)
 * ref: https://github.com/antirez/redis/issues/1861
 */
#include "zpop.h"

// The types of operations this module supports
#define ZPOP_LIST_HEAD 0
#define ZPOP_LIST_TAIL 1

// Statistics
// CBD: average blocking time, unique keys blocked, top blocked keys, ...
#define ZPOP_STAT_EVENTSHANDLED 0
#define ZPOP_STAT_BLOCKEDCLIENTS 1
#define ZPOP_STAT_BLOCKEDREPLIES 2
#define ZPOP_STAT_DISCONNECTIONS 3
#define ZPOP_STAT_TIMEOUTS_COUNT 4
#define ZPOP_STAT_TOTALKEYSBLOCK 5
#define ZPOP_STAT_meta_last 6
// Add any new stats before the last

// The module's global context
// TODO: Once RedisModule_OnUnload is ready, use it on this
typedef struct {
    rax *RK;            // Keys->list of blocked clients
    rax *RBC;           // Blocked clients->keys
    long long *stats;   // Statistics
} gz_t;
static gz_t gz;

/* This is a special pointer that is guaranteed to never have the same value
* of a memory address. It's used in order to report type error without
* requiring the function to have multiple return values. It needs to be
* random-ish so it won't be likely to appear as a string in the data
* Credit: rax.c @antirez */
static void *popTypeError;

// BPOP's blocking client context
typedef struct {
    unsigned char *key;             // The key's name
    size_t keylen;                  // The key's name length
    int lend;                       // The end to POP from
    unsigned char *id;              // The blocked client id
    size_t idlen;                   // The blocked client id length
    RedisModuleBlockedClient *bc;   // The blocked client context
} BPCtx_t;

void freeBPCtx(BPCtx_t *bctx) {
        RedisModule_Free(bctx->key);
        RedisModule_Free(bctx->id);
        RedisModule_Free(bctx);
}

// Converts an unsigned long long to a C buffer
unsigned char *ull2str(unsigned long long ull, size_t *len) {
    char buff[128];
    sprintf(buff, "%llu", ull);

    (*len) = strlen(buff);
    unsigned char *s = RedisModule_Alloc(sizeof(unsigned char) * (*len));
    memcpy(s, buff, (*len));
    return s;
}

// Adds a call reply of a rax data structure
void replyWithRax(RedisModuleCtx *ctx, rax *r) {
    int arrlen = 0;
    RedisModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_ARRAY_LEN);

    // Init the iterator
    raxIterator it;
    raxStart(&it, r);
    raxSeek(&it, "^", NULL, 0);

    while (raxNext(&it)) {
        // Every value is a list of blocking client contexts, so dump them
        RedisModule_ReplyWithArray(ctx, 2); arrlen++;
        RedisModule_ReplyWithStringBuffer(ctx, (const char *)it.key, it.key_len);
        list_t *lbpctx = (list_t *)it.data;
        if (lbpctx->len) {
            RedisModule_ReplyWithArray(ctx, lbpctx->len);
            node_t *n = lbpctx->head;
            while (n) {
                BPCtx_t *bpctx = (BPCtx_t *)n->data;
                RedisModuleString *s = RedisModule_CreateStringPrintf(ctx,
                    "key: %.*s, client: %.*s", bpctx->keylen, bpctx->key, bpctx->idlen, bpctx->id);
                RedisModule_ReplyWithString(ctx, s);
                RedisModule_FreeString(ctx, s);
                n = n->next;
            }
        } else {
            // An empty list - this really shouldn't happen though...
            RedisModule_ReplyWithSimpleString(ctx, "(!)");
        }
    }

    RedisModule_ReplySetArrayLength(ctx, arrlen);    
}

// Adds to the global raxes
void addBlockingClientToKey(RedisModuleString *keyname, unsigned long long id, RedisModuleBlockedClient *bc, int lend) {    
    // Prepeare the blocking pop context
    BPCtx_t *bpctx = RedisModule_Alloc(sizeof(BPCtx_t));
    const char *key = RedisModule_StringPtrLen(keyname, &bpctx->keylen);
    bpctx->key = RedisModule_Alloc(sizeof(unsigned char) * bpctx->keylen);
    memcpy(bpctx->key, key, bpctx->keylen);
    bpctx->lend = lend;
    bpctx->id = ull2str(id, &bpctx->idlen);
    bpctx->bc = bc;

    // Append the context to the list of blocking clients under the key
    list_t *lbc = (list_t *) raxFind(gz.RK, bpctx->key, bpctx->keylen);
    if (raxNotFound == lbc) {
        lbc = listNew();
        raxInsert(gz.RK, bpctx->key, bpctx->keylen, (void *)lbc, NULL);
    }
    listTailPush(lbc, (void *)bpctx);

    // Append the ctx to the list of keys that the client blocks on
    list_t *lk = (list_t *)raxFind(gz.RBC, bpctx->id, bpctx->idlen);
    if (raxNotFound == lk) {
        lk = listNew();
        raxInsert(gz.RBC, bpctx->id, bpctx->idlen, (void *)lk, NULL);
    }
    listTailPush(lk, (void *)bpctx);
}

// Removes from global raxes
void removeBlockingClientFromAllKeys(unsigned char *id, size_t idlen) {    
    // Get the list of keys that the client blocks on
    list_t *lk = (list_t *)raxFind(gz.RBC, id, idlen);
    if (raxNotFound == lk) {
        return;
    }

    // Copy the arguments... we may be freeing the context they're from
    unsigned char *lid = RedisModule_Alloc(sizeof(unsigned char *) * idlen);
    size_t lidlen = idlen;
    memcpy(lid, id, idlen);

    // Iterate these keys, removing the client from each
    while (lk->len) {
        BPCtx_t *bpctx = listHeadPop(lk);

        // Get the iteration's key list of blocking clients
        list_t *lkbc = (list_t *)raxFind(gz.RK, bpctx->key, bpctx->keylen);
        if (raxNotFound == lkbc) {
            continue;
        }

        // Remove current bc from iteration's key
        listRemove(lkbc, bpctx);

        // If the list is now empty, remove it entirely
        if (!lkbc->len) {
            raxRemove(gz.RK, bpctx->key, bpctx->keylen, NULL);
            listFree(lkbc);
        }

        // Free the current bc
        freeBPCtx(bpctx);
    }

    raxRemove(gz.RBC, lid, lidlen, NULL);
    listFree(lk);
}

// Generic ZPOP implemented using only RM_Call() for educational purposes only
// Assumes AutoMajikMemoryManagement
// Returns: array made of two RedisModuleString - the score and the element
// If there's a type error, the array's first item is a 'popTypeError'
RedisModuleString **ZPop_GenericHighLevelAPI(RedisModuleCtx *ctx, RedisModuleString *key, int lend ) {
    // Get the type of the key
    RedisModuleCallReply *type = 
        RedisModule_Call(ctx, "TYPE", "!s", key);
    size_t stypelen = 0;
    const char *stype = RedisModule_CallReplyStringPtr(type, &stypelen);
    
    // Check that the key exists, if not then break early
    if (!strncmp("none", stype, stypelen)) {
        return NULL;
    }

    // Allocate memory for the reply
    RedisModuleString **rep = RedisModule_Alloc(sizeof(RedisModuleString *) * 2);

    // Verify that the key's type is indeed a zset, or return an error
    if (strncmp("zset", stype, stypelen)) {
        rep[0] = popTypeError;
        return rep;
    }

    // Perform the requested zrange operation
    RedisModuleCallReply *arr = NULL;
    if (ZPOP_LIST_HEAD == lend) {
        arr = RedisModule_Call(ctx, "ZRANGE", "!sllc", key, 0, 0, "WITHSCORES");
    } else {
        arr = RedisModule_Call(ctx, "ZREVRANGE", "!sllc", key, 0, 0, "WITHSCORES");
    }

    // Since the zset exists, we know it has at least one element, get it
    RedisModuleString *ele = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(arr, 0));
    RedisModuleString *score = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(arr, 1));

    // Remove that element
    RedisModule_Call(ctx, "ZREM", "ss", key, ele);

    // Return the popped score and element
    rep[0] = score;
    rep[1] = ele;
    return rep;
}

// Generic ZPOP implemented for production with the low level API
// Returns: array made of two RedisModuleString - the score and the element
// If there's a type error, the array's first item is a 'popTypeError'
RedisModuleString **ZPop_GenericLowLevelAPI(RedisModuleCtx *ctx, RedisModuleString *keyname, int lend) {
    // Open the key
    RedisModuleKey *key = RedisModule_OpenKey(ctx, keyname, REDISMODULE_READ | REDISMODULE_WRITE);
    
    // Check that the key exists, if not then break early
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        RedisModule_CloseKey(key);
        return NULL;
    }

    RedisModuleString **rep = RedisModule_Alloc(sizeof(RedisModuleString *) * 2);
    // Verify that the key's type is indeed a zset, or return an error
    if (REDISMODULE_KEYTYPE_ZSET != type)
    {
        RedisModule_CloseKey(key);
        rep[0] = popTypeError;
        return rep;
    }

    // Perform the requested zrange operation, and get the first element
    if (ZPOP_LIST_HEAD == lend) {
        RedisModule_ZsetFirstInScoreRange(key, REDISMODULE_NEGATIVE_INFINITE, REDISMODULE_POSITIVE_INFINITE, 0, 0);
    }
    else {
        RedisModule_ZsetLastInScoreRange(key, REDISMODULE_NEGATIVE_INFINITE, REDISMODULE_POSITIVE_INFINITE, 0, 0);
    }
    double score;
    RedisModuleString *ele = RedisModule_ZsetRangeCurrentElement(key, &score);
    RedisModule_ZsetRangeStop(key);

    // Remove the element
    int deleted;
    RedisModule_ZsetRem(key, ele, &deleted);
    // ASSERT - 1 == deleted ;)

    // The following is a temp workaround for https://github.com/antirez/redis/issues/4859
    if (RedisModule_ValueLength(key) == 0) {
        RedisModule_DeleteKey(key);
    }

    // Lastly, we want to replicate the command's effect
    RedisModule_Replicate(ctx, "ZREM", "ss", keyname, ele);

    // Houskeeping
    RedisModule_CloseKey(key);

    // Prepare and return the reply
    rep[0] = RedisModule_CreateStringPrintf(ctx, "%f", score);
    rep[1] = ele;
    return rep;
}

// A callback to be used when a blocking client is disconnected
void BPop_Disconnected(RedisModuleCtx *ctx, RedisModuleBlockedClient *bc) {
    REDISMODULE_NOT_USED(bc);
    size_t idlen = 0;
    unsigned char *id = ull2str(RedisModule_GetClientId(ctx), &idlen);
    removeBlockingClientFromAllKeys(id, idlen);
    gz.stats[ZPOP_STAT_DISCONNECTIONS]++;
}

// A callback to be used when a blocking client times out
int BPop_Timeout(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    size_t idlen = 0;
    unsigned char *id = ull2str(RedisModule_GetClientId(ctx), &idlen);
    removeBlockingClientFromAllKeys(id, idlen);
    gz.stats[ZPOP_STAT_TIMEOUTS_COUNT]++;

    RedisModule_ReplyWithNull(ctx);
    return REDISMODULE_OK;
}

// A callback to be used for freeing the private data of a blocking client after sending a reply
void BPop_FreeData(RedisModuleCtx *ctx, void *privdata) {
    REDISMODULE_NOT_USED(ctx);
    RedisModuleString **reply = (RedisModuleString **) privdata;
    RedisModule_FreeString(ctx, reply[0]);
    RedisModule_FreeString(ctx, reply[1]);
    RedisModule_Free(privdata);
}

// A callback to be used for sending a reply to the client after unblocking it
int BPop_ReturnReply(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    RedisModuleString **reply = RedisModule_GetBlockedClientPrivateData(ctx);
    RedisModule_ReplyWithArray(ctx, 3);
    RedisModule_ReplyWithString(ctx, reply[0]);
    RedisModule_ReplyWithString(ctx, reply[1]);
    RedisModule_ReplyWithString(ctx, reply[2]);
    gz.stats[ZPOP_STAT_BLOCKEDREPLIES]++;
    return REDISMODULE_OK;
}
// The keyspace events handler for the module
int keySpaceEventsHandler(RedisModuleCtx *ctx, int type, const char *event, RedisModuleString *keyname) {
    size_t keylen = 0;
    const char *key = RedisModule_StringPtrLen(keyname, &keylen);
    gz.stats[ZPOP_STAT_EVENTSHANDLED]++;

    // Is there a key name?
    if (!keylen) {
        return 0;
    }

    // Some commands never create keys, we can break early on them
    // WIP: gotta to map 'em all! (e.g. not SORT, RESTORE, ...) as an optimization
    char *cmdexc[] = {  "del", "exists", "type", // generic commands...
                        "zcard", "zcount", "zlexcount", "zrange",
                        "zrangebylex", "zrangebyscore", "zrank",
                        "zrem", "zremrangebylex", "zremrangebyrank",
                        "zremrangebyscore", "zrevrange", "zrevrangebylex",
                        "zrevrangebyscore", "zrevrank", "zscan", "zscore",
                        NULL};
    int i = 0;
    while (cmdexc[i]) {
        if (!strcmp(event, cmdexc[i])) {
            return 0;
        }
        i++;
    }

    // Check if there are any clients blocking on the key
    list_t *lbc = (list_t *) raxFind(gz.RK, (unsigned char *)key, keylen);

    // As long as the key exists and has blocking clients, we pop for each one
    while (raxNotFound != lbc && lbc->len) {
        // Get the context of the first blocking client on the key
        BPCtx_t *bpctx = (BPCtx_t *)listHeadPop(lbc);

        // ZPop something
        RedisModuleString **rep = ZPop_GenericLowLevelAPI(ctx, keyname, bpctx->lend);

        // The key doesn't actually exist after all, go an block again
        if (NULL == rep) {
            listHeadPush(lbc, (void *)bpctx);
            return 0;
        }

        // The key exists, but is of the wrong type, back to the block
        if (popTypeError == rep) {
            listHeadPush(lbc, (void *)bpctx);
            RedisModule_Free(rep);
            return 0;
        }

        // Unblock the client with the reply
        RedisModuleString **reply = RedisModule_Alloc(sizeof(RedisModuleString *)*3);
        reply[0] = RedisModule_CreateString(ctx, (const char *)bpctx->key, bpctx->keylen);
        reply[1] = rep[0];
        reply[2] = rep[1];
        RedisModule_UnblockClient(bpctx->bc, reply);
        RedisModule_Free(rep);

        // Remove the unblocked context from all its mapped keys
        removeBlockingClientFromAllKeys(bpctx->id, bpctx->idlen);

        // Get the list of blocking clients again
        lbc = (list_t *) raxFind(gz.RK, (unsigned char *)key, keylen);
    }

    // If the list is empty, we can free it and remove the key
    if(raxNotFound != lbc && !lbc->len) {
        raxRemove(gz.RK, (unsigned char *)key, keylen, NULL);
        listFree(lbc);
    }

    return 0;
}

/* Z.[REV]POP <key>
 * Pops the lowest (or highest) ranking member in a single zset, similar to LPOP.
 * Reply: array, or nil when key doesn't exist. The array consists of the popped
 * element's score and the popped element itself.
 */
int Pop_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // Verify that the number of arguments is correct
    if (argc != 2) {
        RedisModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    // Deduce the the end to pop from by examining the command's name
    size_t cmdlen = 0;
    const char *cmd = RedisModule_StringPtrLen(argv[0], &cmdlen);
    int cmdend = (!strcasecmp("z.pop", cmd)) ? ZPOP_LIST_HEAD : ZPOP_LIST_TAIL;

    // Call a generic zpop function
    RedisModuleString **rep = ZPop_GenericLowLevelAPI(ctx, argv[1], cmdend);

    // A null means that the key didn't exists, so we reply with null
    if (NULL == rep) {
        RedisModule_ReplyWithNull(ctx);
        return REDISMODULE_OK;
    }

    // Check for key type errors
    if (popTypeError == rep[0]) {
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
        RedisModule_Free(rep);
    } else {
        // Reply with a an array consisting of the element and its score
        RedisModule_ReplyWithArray(ctx, 2);
        RedisModule_ReplyWithString(ctx, rep[0]);
        RedisModule_ReplyWithString(ctx, rep[1]);
        RedisModule_FreeString(ctx, rep[0]);
        RedisModule_FreeString(ctx, rep[1]);
        RedisModule_Free(rep);
    }
    return REDISMODULE_OK;
}


/* Z.B[REV]POP <key> [<key> ...] <timeout>
 * The blocking variant, similar to BLPOP.
 * Reply: array, or nil when key doesn't exist. The array consists of the popped
 * key, the popped element's score and the popped element itself.
 */
int BPop_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // Verify that the number of arguments is correct
    if (argc < 3) {
        RedisModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    // Handle a "getkey-api" request
    if (RedisModule_IsKeysPositionRequest(ctx)) {
        int i;
        for (i = 1; i < argc - 1; i++) {
            RedisModule_KeyAtPos(ctx, 2);
        }
        return REDISMODULE_OK;
    }

    // Get the timeout from the arguments, and validate it
    long long timeout = 0;
    RedisModule_StringToLongLong(argv[argc-1], &timeout);
    if (timeout < 0) {
        RedisModule_ReplyWithError(ctx, "timeout must be a positive integer");
        return REDISMODULE_OK;
    }

    // Deduce the the end to pop from by examining the command's name
    size_t cmdlen = 0;
    const char *cmd = RedisModule_StringPtrLen(argv[0], &cmdlen);
    int cmdend = (!strcasecmp("z.bpop", cmd)) ? ZPOP_LIST_HEAD : ZPOP_LIST_TAIL;
    
    // Try popping until something happens
    RedisModuleString **rep = NULL;
    int keypos = 1;
    while (keypos < argc - 1) {
        rep = ZPop_GenericLowLevelAPI(ctx, argv[keypos++], cmdend);
        if (NULL == rep) {
            continue;
        }
        if (popTypeError == rep[0]) {
            RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
            RedisModule_Free(rep);
            return REDISMODULE_OK;
        }

        // Popped an element, can return with a reply
        RedisModule_ReplyWithArray(ctx, 2);
        RedisModule_ReplyWithString(ctx, rep[0]);
        RedisModule_ReplyWithString(ctx, rep[1]);
        goto ok;
        
    }

    // Nothing was popped, so go and block
    if (NULL == rep) {
        keypos = 1;
        unsigned long long id = RedisModule_GetClientId(ctx);
        RedisModuleBlockedClient *bc = RedisModule_BlockClient(ctx, BPop_ReturnReply, BPop_Timeout, BPop_FreeData, timeout);
        RedisModule_SetDisconnectCallback(bc, BPop_Disconnected);
        while (keypos < argc - 1) {
            addBlockingClientToKey(argv[keypos], id, bc, cmdend);
            keypos++;
        }
        gz.stats[ZPOP_STAT_BLOCKEDCLIENTS]++;
        gz.stats[ZPOP_STAT_TOTALKEYSBLOCK] += (long long)(argc - 1);
    }

ok:
    // Housekeeping
    if (rep) {
        RedisModule_FreeString(ctx, rep[0]);
        RedisModule_FreeString(ctx, rep[1]);
        RedisModule_Free(rep);
    }

    return REDISMODULE_OK;
}

/* Z.INFO
 * Provides helpful(?) information
 * Reply: array.
 *  - A dump of the internal key->blocked clients mapping
 *  - A dump of the internal blocked client->keys mapping
 *  - Some interesting statistics
 */
int Info_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    RedisModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_ARRAY_LEN);
    int arrlen = 0;

    replyWithRax(ctx, gz.RK); arrlen++;
    replyWithRax(ctx, gz.RBC); arrlen++;

    RedisModule_ReplyWithArray(ctx, 2); arrlen++;
    RedisModule_ReplyWithSimpleString(ctx, "total number of events Z handled");
    RedisModule_ReplyWithLongLong(ctx, gz.stats[ZPOP_STAT_EVENTSHANDLED]);

    RedisModule_ReplyWithArray(ctx, 2); arrlen++;
    RedisModule_ReplyWithSimpleString(ctx, "total number of clients Z blocked");
    RedisModule_ReplyWithLongLong(ctx, gz.stats[ZPOP_STAT_BLOCKEDCLIENTS]);

    RedisModule_ReplyWithArray(ctx, 2); arrlen++;
    RedisModule_ReplyWithSimpleString(ctx, "total number of replies Z sent to unblocked clients");
    RedisModule_ReplyWithLongLong(ctx, gz.stats[ZPOP_STAT_BLOCKEDREPLIES]);

    RedisModule_ReplyWithArray(ctx, 2); arrlen++;
    RedisModule_ReplyWithSimpleString(ctx, "total number of blocked clients who disconnected from Z");
    RedisModule_ReplyWithLongLong(ctx, gz.stats[ZPOP_STAT_DISCONNECTIONS]);

    RedisModule_ReplyWithArray(ctx, 2); arrlen++;
    RedisModule_ReplyWithSimpleString(ctx, "total number of blocked clients who timed out on Z");
    RedisModule_ReplyWithLongLong(ctx, gz.stats[ZPOP_STAT_TIMEOUTS_COUNT]);

    RedisModule_ReplyWithArray(ctx, 2); arrlen++;
    RedisModule_ReplyWithSimpleString(ctx, "total number of keys Z watched");
    RedisModule_ReplyWithLongLong(ctx, gz.stats[ZPOP_STAT_TOTALKEYSBLOCK]);

    RedisModule_ReplySetArrayLength(ctx, arrlen);

    return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    // Register the module
    if (RedisModule_Init(ctx,"zpop",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    // Register the commands
    if (RedisModule_CreateCommand(ctx,"z.info",
        Info_RedisCommand,"readonly",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"z.pop",
        Pop_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"z.revpop",
        Pop_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"z.bpop",
        BPop_RedisCommand,"write getkeys-api",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"z.brevpop",
        BPop_RedisCommand,"write getkeys-api",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    // Initialize the globals
    popTypeError = (void*)"z-pop-type-error-special-pointer-426144";
    gz.RK = raxNew();
    gz.RBC = raxNew();
    gz.stats = RedisModule_Alloc(sizeof(long long) * ZPOP_STAT_meta_last);
    for (int i = 0; i < ZPOP_STAT_meta_last; i++) {
        gz.stats[i] = 0;
    }

    // Register the keyspace notifications handler
    int mask = (REDISMODULE_NOTIFY_GENERIC | REDISMODULE_NOTIFY_ZSET);
    RedisModule_SubscribeToKeyspaceEvents(ctx, mask, keySpaceEventsHandler);

    // Make a happy sound
    RedisModule_Log(ctx,"info","zpop module loaded - woot woot woot!");

    return REDISMODULE_OK;
}
