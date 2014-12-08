/****************************************************************************
 * Copyright (c) 2012-2014, Masahiro Ide <ide@konohascript.org>
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#include <stdint.h>
#include <assert.h>
#include "internal.h" // nlz_long

#ifdef __cplusplus
extern "C" {
#endif

// This 64-bit-to-32-bit hash was copied from
// http://www.concentric.net/~Ttwang/tech/inthash.htm .
static unsigned hash6432shift(long key)
{
    key = (~key) + (key << 18); // key = (key << 18) - key - 1;
    key = key ^ (key >> 31);
    key = key * 21; // key = (key + (key << 2)) + (key << 4);
    key = key ^ (key >> 11);
    key = key + (key << 6);
    key = key ^ (key >> 22);
    return (unsigned)key;
}

typedef long hashmap_data_t;

#define DELTA 4
#define HASHMAP_INITSIZE 128
#ifndef LOG2
#define LOG2(N) ((uint32_t)((sizeof(void *) * 8) - nlz_long(N - 1)))
#endif

typedef enum hashmap_status_t {
    HASHMAP_FAILED = 0,
    HASHMAP_UPDATE = 1,
    HASHMAP_ADDED = 2
} hashmap_status_t;

typedef struct hashmap_record {
    unsigned hash;
    hashmap_data_t key;
    hashmap_data_t val;
} __attribute__((__aligned__(8))) hashmap_record_t;

typedef struct hashmap_t {
    hashmap_record_t *records;
    unsigned used_size;
    unsigned record_size_mask;
} hashmap_t;

typedef struct hashmap_iterator {
    hashmap_record_t *entry;
    unsigned index;
} hashmap_iterator_t;

typedef void (*hashmap_entry_destructor_t)(hashmap_data_t);

static inline unsigned hashmap_size(hashmap_t *m)
{
    return m->used_size;
}

static inline unsigned hashmap_capacity(hashmap_t *m)
{
    return m->record_size_mask + 1;
}

static void hashmap_record_copy(hashmap_record_t *dst,
                                const hashmap_record_t *src)
{
    dst->hash = src->hash;
    dst->key = src->key;
    dst->val = src->val;
}

static inline hashmap_record_t *hashmap_at(hashmap_t *m, unsigned idx)
{
    assert(idx < hashmap_capacity(m));
    return m->records + idx;
}

static void hashmap_record_reset(hashmap_t *m, unsigned newsize)
{
    unsigned alloc_size = (unsigned)(newsize * sizeof(hashmap_record_t));
    m->used_size = 0;
    (m->record_size_mask) = newsize - 1;
    m->records = (hashmap_record_t *)calloc(1, alloc_size);
}

static hashmap_status_t hashmap_set_no_resize(hashmap_t *m,
                                              hashmap_record_t *rec)
{
    unsigned i, idx = rec->hash & m->record_size_mask;
    for (i = 0; i < DELTA; ++i) {
	hashmap_record_t *r = m->records + idx;
	if (r->hash == 0) {
	    hashmap_record_copy(r, rec);
	    ++m->used_size;
	    return HASHMAP_ADDED;
	}
	if (r->hash == rec->hash && r->key == rec->key) {
	    hashmap_record_copy(r, rec);
	    return HASHMAP_UPDATE;
	}
	idx = (idx + 1) & m->record_size_mask;
    }
    return HASHMAP_FAILED;
}

static void hashmap_record_resize(hashmap_t *m, unsigned newsize)
{
    unsigned i;
    unsigned oldsize = hashmap_capacity(m);
    hashmap_record_t *head = m->records;

    hashmap_record_reset(m, newsize);
    for (i = 0; i < oldsize; ++i) {
	hashmap_record_t *r = head + i;
	if (r->hash && hashmap_set_no_resize(m, r) == HASHMAP_FAILED) {
	    continue;
	}
    }
    free(head);
}

static void hashmap_set_(hashmap_t *m, hashmap_record_t *rec)
{
    do {
	if ((hashmap_set_no_resize(m, rec)) != HASHMAP_FAILED)
	    return;
	hashmap_record_resize(m, hashmap_capacity(m) * 2);
    } while (1);
}

static hashmap_data_t hashmap_get_(hashmap_t *m, unsigned hash,
                                   hashmap_data_t key)
{
    unsigned i, idx = hash & m->record_size_mask;
    for (i = 0; i < DELTA; ++i) {
	hashmap_record_t *r = m->records + idx;
	if (r->hash == hash && r->key == key) {
	    return r->val;
	}
	idx = (idx + 1) & m->record_size_mask;
    }
    return 0;
}

static void hashmap_init(hashmap_t *m, unsigned init)
{
    if (init < HASHMAP_INITSIZE)
	init = HASHMAP_INITSIZE;
    hashmap_record_reset(m, 1U << LOG2(init));
}

static void hashmap_dispose(hashmap_t *m, hashmap_entry_destructor_t Destructor)
{
    unsigned i, size = hashmap_capacity(m);
    if (Destructor) {
	for (i = 0; i < size; ++i) {
	    hashmap_record_t *r = hashmap_at(m, i);
	    if (r->hash) {
		Destructor(r->val);
	    }
	}
    }
    free(m->records);
    m->used_size = m->record_size_mask = 0;
    m->records = NULL;
}

static hashmap_data_t hashmap_get(hashmap_t *m, hashmap_data_t key)
{
    unsigned hash = hash6432shift(key);
    return hashmap_get_(m, hash, key);
}

static void hashmap_set(hashmap_t *m, hashmap_data_t key, hashmap_data_t val)
{
    hashmap_record_t r;
    r.hash = hash6432shift(key);
    r.key = key;
    r.val = val;
    hashmap_set_(m, &r);
}

static int hashmap_next(hashmap_t *m, hashmap_iterator_t *itr)
{
    unsigned idx, size = hashmap_capacity(m);
    for (idx = itr->index; idx < size; idx++) {
	hashmap_record_t *r = hashmap_at(m, idx);
	if (r->hash != 0) {
	    itr->index = idx + 1;
	    itr->entry = r;
	    return 1;
	}
    }
    return 0;
}

static void hashmap_remove(hashmap_t *m, hashmap_data_t key, hashmap_entry_destructor_t Destructor)
{
    unsigned hash = hash6432shift(key);
    unsigned i, idx = hash & m->record_size_mask;
    for (i = 0; i < DELTA; ++i) {
	hashmap_record_t *r = hashmap_at(m, idx);
	if (r->hash == hash && r->key == key) {
	    if (Destructor) {
		Destructor(r->val);
	    }
	    r->hash = 0;
	    r->key = 0;
	    m->used_size -= 1;
	    return;
	}
	idx = (idx + 1) & m->record_size_mask;
    }
}

#ifdef __cplusplus
} /* extern "C" */
#endif
