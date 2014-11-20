static inline void *jit_malloc(size_t size)
{
    void *p = malloc(size);
    fprintf(stderr, "M %p, %p\n", p, (char *)p + size);
    return p;
}

static inline void *jit_realloc(void *p, size_t size)
{
    void *newp = realloc(p, size);
    fprintf(stderr, "R %p, %p, %p\n", p, newp, (char *)newp + size);
    return newp;
}

static inline void jit_free(void *p)
{
    fprintf(stderr, "F %p\n", p);
    free(p);
}

#define malloc(X) jit_malloc(X)
#define realloc(X, SIZE) jit_realloc(X, SIZE)
#define free(X) jit_free(X)
