#ifndef RAX_ALLOC_H
#define RAX_ALLOC_H
#include "redismodule.h"
#define rax_malloc RedisModule_Alloc
#define rax_realloc RedisModule_Realloc
#define rax_free RedisModule_Free
#endif
