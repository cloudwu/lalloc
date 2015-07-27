#ifndef skynet_lua_alloc_h
#define skynet_lua_alloc_h

#include <stddef.h>

struct allocator;

struct allocator * allocator_new();
void allocator_delete(struct allocator *A);

void * skynet_lalloc(void *ud, void *ptr, size_t osize, size_t nsize);
void allocator_info(struct allocator *A);

#endif
