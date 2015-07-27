#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "testcase.h"
#include "lualloc.h"

extern int STACK, N;
extern struct test_item ITEM[];

static void
fill(char *p, int sz) {
	char dummy[8];
	uintptr_t x = (uintptr_t)p;
	int i;
	for (i=0;i<8;i++) {
		dummy[i] = x & 0xff;
		x >>= 8;
	}
	for (i=0;i<sz;i++) {
		p[i] = (dummy[i % sizeof(uintptr_t)] ^ sz) & 0xff;
	}
}

static int
check(char *p, int sz) {
	char dummy[8];
	uintptr_t x = (uintptr_t)p;
	int i;
	for (i=0;i<8;i++) {
		dummy[i] = x & 0xff;
		x >>= 8;
	}
	for (i=0;i<sz;i++) {
		char c = (dummy[i % sizeof(uintptr_t)] ^ sz) & 0xff;
		if (c != p[i])
			return 1;
	}
	return 0;
}

static void
test(struct allocator *ud) {
	int i;
	char ** ptr = (char **)malloc(STACK * sizeof(char *));
	for (i=0;i<N;i++) {
		struct test_item * item = &ITEM[i];
		int index = item->index - 1;
		switch(item->action) {
		case TEST_ALLOC:
			ptr[index] = skynet_lalloc(ud, NULL, 0, item->size);
			fill(ptr[index], item->size);
			break;
		case TEST_FREE:
			if (check(ptr[index], item->size)) {
				printf("block %d (%d)error\n", index, item->size);
			}
			memset(ptr[index] , 0x77, item->size);
			skynet_lalloc(ud, ptr[index], item->size, 0);
			break;
		case TEST_REALLOC: {
			char * oldptr = ptr[index];
			if (check(oldptr, item->osize)) {
				printf("realloc block %d (%d -> %d)error\n", index, item->osize, item->size);
			}
			ptr[index] = skynet_lalloc(ud, oldptr, item->osize, item->size);
			fill(ptr[index], item->size);
			break;
		}
		}
	}
	free(ptr);
}

static void *
malloc_lalloc(void *ud, void *ptr, size_t osize, size_t nsize) {
	if (nsize == 0) {
		free(ptr);
		return NULL;
	} else {
		return realloc(ptr, nsize);
	}
}

static void
test_nocheck(struct allocator *ud, void * (*lalloc)(void *, void *, size_t, size_t)) {
	int i;
	char ** ptr = (char **)malloc(STACK * sizeof(char *));
	for (i=0;i<N;i++) {
		struct test_item * item = &ITEM[i];
		int index = item->index - 1;
		switch(item->action) {
		case TEST_ALLOC:
			ptr[index] = lalloc(ud, NULL, 0, item->size);
			break;
		case TEST_FREE:
			lalloc(ud, ptr[index], item->size, 0);
			break;
		case TEST_REALLOC: {
			char * oldptr = ptr[index];
			ptr[index] = lalloc(ud, oldptr, item->osize, item->size);
			break;
		}
		}
	}
	free(ptr);
}

#include "jemalloc.h"

static void *
jemalloc_lalloc(void *ud, void *ptr, size_t osize, size_t nsize) {
	if (nsize == 0) {
		je_free(ptr);
		return NULL;
	} else {
		return je_realloc(ptr, nsize);
	}
}

int
main(int argc, char *argv[]) {
	if (argc <= 1)
		return 1;
	if (strcmp(argv[1],"skynet") == 0) {
		struct allocator *A = allocator_new();
		test_nocheck(A, skynet_lalloc);
		allocator_info(A);
		allocator_delete(A);
	} else if (strcmp(argv[1], "malloc") == 0) {
		test_nocheck(NULL, malloc_lalloc);
	} else if (strcmp(argv[1], "jemalloc") == 0) {
		test_nocheck(NULL, jemalloc_lalloc);
	} else if (strcmp(argv[1], "check") == 0) {
		struct allocator *A = allocator_new();
		test(A);
		allocator_info(A);
		allocator_delete(A);
	}
	
	return 0;
}

