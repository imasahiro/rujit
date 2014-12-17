/**********************************************************************

  jit_utils.c -

  $Author$

  Copyright (C) 2014 Masahiro Ide

**********************************************************************/

#include "jit_hashmap.h"
#include "jit_profile.h"

typedef struct memory_pool {
    struct page_chunk *head;
    struct page_chunk *root;
    unsigned pos;
    unsigned size;
} memory_pool_t;

/* memory pool { */
#define SIZEOF_MEMORY_POOL (4096 - sizeof(void *))
struct page_chunk {
    struct page_chunk *next;
    char buf[SIZEOF_MEMORY_POOL];
};

static void memory_pool_init(struct memory_pool *mp)
{
    mp->pos = 0;
    mp->size = 0;
    mp->head = mp->root = NULL;
}

static void memory_pool_reset(struct memory_pool *mp, int alloc_memory)
{
    struct page_chunk *root;
    struct page_chunk *page;
    root = mp->root;
    mp->size = 0;
    if (root) {
	page = root->next;
	while (page != NULL) {
	    struct page_chunk *next = page->next;
	    memset(page, -1, sizeof(struct page_chunk));
	    free(page);
	    page = next;
	}
	if (!alloc_memory) {
	    free(root);
	    root = NULL;
	}
	mp->pos = 0;
	mp->size = SIZEOF_MEMORY_POOL;
    }
    mp->head = mp->root = root;
}

#define MEMORY_POOL_ALLOC(TYPE, MP) ((TYPE *)memory_pool_alloc(MP, sizeof(TYPE)))

static void *memory_pool_alloc(struct memory_pool *mp, size_t size)
{
    void *ptr;
    if (mp->pos + size > mp->size) {
	struct page_chunk *page;
	page = (struct page_chunk *)malloc(sizeof(struct page_chunk));
	page->next = NULL;
	if (mp->head) {
	    mp->head->next = page;
	}
	mp->head = page;
	mp->pos = 0;
	mp->size = SIZEOF_MEMORY_POOL;
    }
    ptr = mp->head->buf + mp->pos;
    memset(ptr, 0, size);
    mp->pos += size;
    return ptr;
}
/* } memory pool */

/* bloom_filter { */
typedef struct bloom_filter {
    uintptr_t bits;
} bloom_filter_t;

/* TODO(imasahiro) check hit/miss ratio and find hash algorithm for pointer */
static bloom_filter_t *bloom_filter_init(bloom_filter_t *bm)
{
    bm->bits = 0;
    return bm;
}

static void bloom_filter_add(bloom_filter_t *bm, uintptr_t bits)
{
    JIT_PROFILE_COUNT(invoke_bloomfilter_entry);
    bm->bits |= bits;
}

static int bloom_filter_contains(bloom_filter_t *bm, uintptr_t bits)
{
    JIT_PROFILE_COUNT(invoke_bloomfilter_total);
    if ((bm->bits & bits) == bits) {
	JIT_PROFILE_COUNT(invoke_bloomfilter_hit);
	return 1;
    }
    return 0;
}
/* } bloom_filter */

/* jit_list { */
typedef struct jit_list {
    uintptr_t *list;
    unsigned size;
    unsigned capacity;
} jit_list_t;

#define JIT_LIST_ADD(LIST, VAL) jit_list_add(LIST, (uintptr_t)VAL)
#define JIT_LIST_GET(TYPE, LIST, IDX) ((TYPE)jit_list_get(LIST, IDX))
#define JIT_LIST_INSERT(LIST, IDX, VAL) jit_list_insert(LIST, IDX, (uintptr_t)VAL)
#define JIT_LIST_INDEXOF(LIST, VAL) jit_list_indexof(LIST, (uintptr_t)VAL)
#define JIT_LIST_SET(LIST, IDX, VAL) jit_list_set(LIST, IDX, (uintptr_t)VAL)
#define JIT_LIST_REMOVE(LIST, VAL) jit_list_remove(LIST, (uintptr_t)VAL)

static void jit_list_init(jit_list_t *self)
{
    self->list = NULL;
    self->size = self->capacity = 0;
}

static unsigned jit_list_size(jit_list_t *self)
{
    return self->size;
}

static void jit_list_clear(jit_list_t *self)
{
    self->size = 0;
}

static uintptr_t jit_list_get(jit_list_t *self, int idx)
{
    assert(0 <= idx && idx < (int)self->size);
    return self->list[idx];
}

// static uintptr_t jit_list_get_or_null(jit_list_t *self, int idx)
// {
//     if (0 <= idx && idx < (int)self->size) {
// 	return jit_list_get(self, idx);
//     }
//     return 0;
// }

static void jit_list_set(jit_list_t *self, int idx, uintptr_t val)
{
    assert(0 <= idx && idx < (int)self->size);
    self->list[idx] = val;
}

static int jit_list_indexof(jit_list_t *self, uintptr_t val)
{
    unsigned i;
    for (i = 0; i < self->size; i++) {
	if (self->list[i] == (uintptr_t)val) {
	    return i;
	}
    }
    return -1;
}

static void jit_list_ensure(jit_list_t *self, unsigned capacity)
{
    if (capacity > self->capacity) {
	if (self->capacity == 0) {
	    self->capacity = capacity;
	    self->list = (uintptr_t *)malloc(sizeof(uintptr_t) * self->capacity);
	}
	else {
	    while (capacity > self->capacity) {
		self->capacity *= 2;
	    }
	    self->list = (uintptr_t *)realloc(self->list, sizeof(uintptr_t) * self->capacity);
	}
    }
}

static void jit_list_insert(jit_list_t *self, unsigned idx, uintptr_t val)
{
    jit_list_ensure(self, self->size + 1);
    memmove(self->list + idx + 1, self->list + idx, sizeof(uintptr_t) * (self->size - idx));
    self->size++;
    self->list[idx] = val;
}

static void jit_list_add(jit_list_t *self, uintptr_t val)
{
    jit_list_ensure(self, self->size + 1);
    self->list[self->size++] = val;
}

// static void jit_list_safe_set(jit_list_t *self, unsigned idx, uintptr_t val)
// {
//     jit_list_ensure(self, self->size + 1);
//     while (idx >= self->size) {
// 	jit_list_add(self, 0);
//     }
//     jit_list_set(self, idx, val);
// }

static uintptr_t jit_list_remove_idx(jit_list_t *self, int idx)
{
    uintptr_t val = 0;
    if (0 <= idx && idx < (int)self->size) {
	val = jit_list_get(self, idx);
	self->size -= 1;
	memmove(self->list + idx, self->list + idx + 1,
	        sizeof(uintptr_t) * (self->size - idx));
    }
    return val;
}

static uintptr_t jit_list_remove(jit_list_t *self, uintptr_t val)
{
    int i;
    uintptr_t ret = 0;
    if ((i = jit_list_indexof(self, val)) != -1) {
	ret = jit_list_remove_idx(self, i);
    }
    return ret;
}

static int jit_list_equal(jit_list_t *l1, jit_list_t *l2)
{
    unsigned i;
    if (l1 == l2) {
	return 1;
    }
    if (l1 == NULL || l2 == NULL) {
	return 0;
    }
    if (l1->size != l2->size) {
	return 0;
    }
    for (i = 0; i < l1->size; i++) {
	uintptr_t e1 = jit_list_get(l1, i);
	uintptr_t e2 = jit_list_get(l2, i);
	if (e1 != e2) {
	    return 0;
	}
    }
    return 1;
}

static void jit_list_delete(jit_list_t *self)
{
    if (self->capacity) {
	free(self->list);
	self->list = NULL;
    }
    self->size = self->capacity = 0;
}
static void jit_list_free(jit_list_t *self)
{
    jit_list_delete(self);
    free(self);
}

/* } jit_list */
