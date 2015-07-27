#define _GNU_SOURCE
#include <sys/mman.h>

#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include "lualloc.h"

#define SMALLSIZE 8	// should be pow of 2, and at lease sizeof(size_t).
#define SMALLLEVEL 32	// [1,256]
#define CHUNKSIZE (32 * 1024)
#define HUGESIZE (CHUNKSIZE - 16)	// 16 means chunk cookie (sizeof(chunk)) + block cookie (size_t)
#define BIGSEARCHDEPTH 128

struct chunk {
	struct chunk * next;
};

struct smallblock {
	struct smallblock * next;
};

struct bigblock {
	size_t sz;	// used block only has sz field
	struct bigblock * next;	// free block has next field
};

struct hugeblock {
	struct hugeblock * prev;
	struct hugeblock * next;
	size_t sz;
};

struct allocator {
	struct chunk base;	// allocator is a chunk, base must be the first
	struct smallblock * small_list[SMALLLEVEL+1];
	struct chunk * chunk_list;
	struct bigblock * big_head;
	struct bigblock * big_tail;
	struct hugeblock huge_list;
	int chunk_used;
};

static inline void *
alloc_page(size_t sz) {
	return mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

struct allocator *
allocator_new() {
	struct allocator * A = alloc_page(CHUNKSIZE);
	memset(A, 0, sizeof(*A));
	A->chunk_used = sizeof(struct allocator);
	A->chunk_list = &(A->base);
	A->huge_list.prev = A->huge_list.next = &A->huge_list;
	return A;
}

void
allocator_delete(struct allocator *A) {
	struct hugeblock * h = A->huge_list.next;
	while (h != &(A->huge_list)) {
		struct hugeblock * nh = h->next;
		munmap(h, h->sz);
		h = nh;
	}
	// A should be the last chunk
	struct chunk * c = A->chunk_list;
	while (c) {
		struct chunk * nc = c->next;
		munmap(c, CHUNKSIZE);
		c = nc;
	}
}

void
allocator_info(struct allocator *A) {
	struct hugeblock * h = A->huge_list.next;
	while (h != &(A->huge_list)) {
		struct hugeblock * nh = h->next;
		printf("huge block %d %d\n", (int)h->sz, ((int)h->sz + 4095)/4096);
		h = nh;
	}
	// A should be the last chunk
	struct chunk * c = A->chunk_list;
	int n = 0;
	while (c) {
		++n;
		c = c->next;
	}
	printf("chunk page %d %d\n", CHUNKSIZE, n);
}

static inline void *
new_chunk(struct allocator * A, int sz) {
	// alloc new chunk
	struct chunk * nc = alloc_page(CHUNKSIZE);
	if (nc == NULL)
		return NULL;
	nc->next = A->chunk_list;
	A->chunk_list = nc;
	A->chunk_used = sizeof(struct chunk) + sz;
	return nc+1;
}

static void *
memory_allocsmall(struct allocator * A, int n) {
	struct smallblock * node = A->small_list[n];
	if (node) {
		A->small_list[n] = node->next;
		return node;
	}
	int sz = (n+1) * SMALLSIZE;
	if (A->chunk_used + sz <= CHUNKSIZE) {
		void * ret = (char *)A->chunk_list + A->chunk_used;
		A->chunk_used += sz;
		return ret;
	}

	// lookup larger small list
	int i;
	for (i = n + 1; i<= SMALLLEVEL; i++) {
		void * ret = A->small_list[i];
		if (ret) {
			A->small_list[i] = A->small_list[i]->next;
			int idx = i - n - 1;
			struct smallblock * sb = (struct smallblock *)((char *)ret + sz);
			sb->next = A->small_list[idx];
			A->small_list[idx] = sb;
			return ret;			
		}
	}

	return new_chunk(A, sz);
}

static inline void
memory_freesmall(struct allocator * A, struct smallblock *ptr, int n) {
	ptr->next = A->small_list[n];
	A->small_list[n] = ptr;
}

static void *
memory_allochuge(struct allocator * A, size_t sz) {
	struct hugeblock * h = alloc_page(sizeof(struct hugeblock) + sz);
	if (h == NULL)
		return NULL;
	h->prev = &A->huge_list;
	h->next = A->huge_list.next;
	h->sz = sz;
	A->huge_list.next->prev = h;
	A->huge_list.next = h;
	return h+1;
}

static void
memory_freehuge(struct allocator *A,  struct hugeblock * ptr) {
	--ptr;
	ptr->prev->next = ptr->next;
	ptr->next->prev = ptr->prev;
	munmap(ptr, ptr->sz + sizeof(struct hugeblock));
}

static struct bigblock *
lookup_biglist(struct allocator *A, int sz) {
	if (A->big_head == NULL)
		return NULL;
	struct bigblock *b = A->big_head;
	if (b == A->big_tail) {
		// only one node in big list
		if (b->sz >= sz) {
			int f = b->sz - sz;
			if (f == 0) {
				A->big_head = A->big_tail = NULL;
				return b;
			}
			int idx = (f - 1) / SMALLSIZE;
			void * ptr = (char *)b + sz;
			if (idx < SMALLLEVEL) {
				memory_freesmall(A, ptr, idx);
				A->big_head = A->big_tail = NULL;
			} else {
				A->big_head = A->big_tail = ptr;
			}
			return b;
		}
		return NULL;		
	}

	struct bigblock *term = b;
	int n = 0;
	do {
		// remove from head
		A->big_head = b->next;
		if (b->sz >= sz) {
			// find one suitable
			if (b->sz == sz)
				return b;
			int f = b->sz - sz;
			b->sz = sz;
			// split
			int idx = (f - 1) / SMALLSIZE;
			void * ptr = (char *)b + sz;
			if (idx < SMALLLEVEL) {
				memory_freesmall(A, ptr, idx);
			} else {
				struct bigblock * lastpart = ptr;
				lastpart->sz = f;
				if (f > sz) {
					// move lastpart (larger part) to head
					lastpart->next = A->big_head;
					A->big_head = lastpart;
				} else {
					// move lastpart (small part) to tail
					lastpart->next = NULL;
					A->big_tail->next = lastpart;
					A->big_tail = lastpart;
				}
			}
			return b;
		}
		// add b to tail
		b->next = NULL;
		A->big_tail->next = b;
		A->big_tail = b;
		// read head
		b = A->big_head;
		++n;
		// don't search too depth
	} while (b != term && n < BIGSEARCHDEPTH);
/*
	if (b != term) {
		while (b) {
			if (b->sz >= sz) {
				printf("miss\n");
				break;
			}
			b=b->next;
		}
	}
*/
	return NULL;
}

static void *
memory_allocbig(struct allocator *A, int sz) {
	sz = (sz + sizeof(size_t) + 7) & ~7;	// align to 8
	// lookup the last big chunk
	if (A->chunk_used + sz <= CHUNKSIZE) {
		void * b = (char *)A->chunk_list + A->chunk_used;
		A->chunk_used += sz;
		struct bigblock *bb = b;
		bb->sz = sz;
		return (char *)b + sizeof(size_t);
	}
	// lookup big list
	struct bigblock * b = lookup_biglist(A, sz);
	if (b == NULL) {
		b = new_chunk(A, (int)sz);
		if (b == NULL)
			return NULL;
		b->sz = sz;
	}
	return (char *)b + sizeof(size_t);
}

static inline void
memory_freebig(struct allocator *A, void *ptr) {
	struct bigblock *b = (struct bigblock *)((char *)ptr - sizeof(size_t));
	if (A->big_head == NULL) {
		A->big_head = A->big_tail = b;
		b->next = NULL;
	} else {
		b->next = A->big_head;
		A->big_head = b;
	}
}

static inline void *
memory_alloc(void *ud, size_t nsize) {
	if (nsize <= SMALLLEVEL * SMALLSIZE) {
		if (nsize == 0)
			return NULL;
		int idx = ((int)(nsize) - 1) / SMALLSIZE;
		return memory_allocsmall(ud, idx);
	} else {
		if (nsize > HUGESIZE) {
			return memory_allochuge(ud, nsize);
		} else {
			return memory_allocbig(ud, (int)nsize);
		}
	}
}

static inline void
memory_free(void *ud, void *ptr, size_t osize) {
	if (osize <= SMALLLEVEL * SMALLSIZE) {
		// osize never be 0
		memory_freesmall(ud, ptr, (osize - 1) / SMALLSIZE);
	} else if (osize > HUGESIZE) {
		memory_freehuge(ud, ptr);
	} else {
		memory_freebig(ud, ptr);
	}
}

static void *
memory_reallochuge(struct allocator *A, void *ptr, size_t nsize) {
	struct hugeblock *h = (struct hugeblock *)ptr - 1;
	struct hugeblock *nh = mremap(h, h->sz + sizeof(struct hugeblock), 
		nsize + sizeof(struct hugeblock), MREMAP_MAYMOVE);
	if (nh == NULL)
		return NULL;
	nh->sz = nsize;
	if (h == nh) {
		return ptr;
	}
	nh->prev->next = nh;
	nh->next->prev = nh;
	return nh + 1;
}

void *
skynet_lalloc(void *ud, void *ptr, size_t osize, size_t nsize) {
	if (ptr == NULL) {
		return memory_alloc(ud, nsize);
	} else if (nsize == 0) {
		memory_free(ud, ptr, osize);
		return NULL;
	} else {
		if (osize > HUGESIZE && nsize > HUGESIZE) {
			return memory_reallochuge(ud, ptr, nsize);
		} else if (nsize <= osize) {
			return ptr;
		} else {
			void * tmp = memory_alloc(ud, nsize);
			if (tmp == NULL)
				return NULL;
			memcpy(tmp, ptr, osize);
			memory_free(ud, ptr, osize);
			return tmp;
		}
	}
}

