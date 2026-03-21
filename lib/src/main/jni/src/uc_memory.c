#include "uc_memory.h"
#include "uc_vm.h"

typedef struct {
    uint32_t next;
    uint32_t len;
} LG_mem_free_t;

static uint32_t LG_mem_min;
static uint32_t LG_mem_top;
static LG_mem_free_t LG_mem_free;
static char *LG_mem_base;
static uint32_t LG_mem_len;
static char *Origin_LG_mem_base;
static uint32_t Origin_LG_mem_len;
static char *LG_mem_end;
static uint32_t LG_mem_left;

#define realLGmemSize(x) (((x) + 7) & (0xfffffff8))

void uc_mem_manager_init(uint32_t baseAddress, uint32_t len) {
    Origin_LG_mem_base = uc_getMrpMemPtr(baseAddress);
    Origin_LG_mem_len = len;

    LG_mem_base = (char *)((uintptr_t)(Origin_LG_mem_base + 3) & (~(uintptr_t)3));
    LG_mem_len = (Origin_LG_mem_len - (LG_mem_base - Origin_LG_mem_base)) & (~3);
    LG_mem_end = LG_mem_base + LG_mem_len;
    LG_mem_free.next = 0;
    LG_mem_free.len = 0;
    ((LG_mem_free_t *)LG_mem_base)->next = LG_mem_len;
    ((LG_mem_free_t *)LG_mem_base)->len = LG_mem_len;
    LG_mem_left = LG_mem_len;
    LG_mem_min = LG_mem_len;
    LG_mem_top = 0;
}

void uc_printMemoryInfo(void) {
    /* intentionally empty for Android - use logcat instead */
}

static void *uc_my_malloc(uint32_t len) {
    LG_mem_free_t *previous, *nextfree, *l;
    void *ret;

    len = (uint32_t)realLGmemSize(len);
    if (len >= LG_mem_left)
        return NULL;
    if (!len)
        return NULL;
    if (LG_mem_base + LG_mem_free.next > LG_mem_end)
        return NULL;

    previous = &LG_mem_free;
    nextfree = (LG_mem_free_t *)(LG_mem_base + previous->next);
    while ((char *)nextfree < LG_mem_end) {
        if (nextfree->len == len) {
            previous->next = nextfree->next;
            LG_mem_left -= len;
            if (LG_mem_left < LG_mem_min)
                LG_mem_min = LG_mem_left;
            if (LG_mem_top < previous->next)
                LG_mem_top = previous->next;
            return (void *)nextfree;
        }
        if (nextfree->len > len) {
            l = (LG_mem_free_t *)((char *)nextfree + len);
            l->next = nextfree->next;
            l->len = (uint32_t)(nextfree->len - len);
            previous->next += len;
            LG_mem_left -= len;
            if (LG_mem_left < LG_mem_min)
                LG_mem_min = LG_mem_left;
            if (LG_mem_top < previous->next)
                LG_mem_top = previous->next;
            return (void *)nextfree;
        }
        previous = nextfree;
        nextfree = (LG_mem_free_t *)(LG_mem_base + nextfree->next);
    }
    return NULL;
}

static void uc_my_free(void *p, uint32_t len) {
    LG_mem_free_t *f, *n;
    len = (uint32_t)realLGmemSize(len);

    if (!len || !p || (char *)p < LG_mem_base || (char *)p >= LG_mem_end ||
        (char *)p + len > LG_mem_end || (char *)p + len <= LG_mem_base)
        return;

    f = &LG_mem_free;
    n = (LG_mem_free_t *)(LG_mem_base + f->next);
    while (((char *)n < LG_mem_end) && ((void *)n < p)) {
        f = n;
        n = (LG_mem_free_t *)(LG_mem_base + n->next);
    }

    if (p == (void *)f || p == (void *)n)
        return;

    if ((f != &LG_mem_free) && ((char *)f + f->len == p)) {
        f->len += len;
    } else {
        f->next = (uint32_t)((char *)p - LG_mem_base);
        f = (LG_mem_free_t *)p;
        f->next = (uint32_t)((char *)n - LG_mem_base);
        f->len = len;
    }
    if (((char *)n < LG_mem_end) && ((char *)p + len == (char *)n)) {
        f->next = n->next;
        f->len += n->len;
    }
    LG_mem_left += len;
}

void *uc_my_mallocExt(uint32_t len) {
    uint32_t *p;
    if (len == 0)
        return NULL;
    p = uc_my_malloc(len + sizeof(uint32_t));
    if (p) {
        *p = len;
        return (void *)(p + 1);
    }
    return NULL;
}

void uc_my_freeExt(void *p) {
    if (p) {
        uint32_t *t = (uint32_t *)p - 1;
        uc_my_free(t, *t + sizeof(uint32_t));
    }
}

void *uc_my_reallocExt(void *p, uint32_t newLen) {
    if (p == NULL)
        return uc_my_mallocExt(newLen);
    if (newLen == 0) {
        uc_my_freeExt(p);
        return NULL;
    }
    uint32_t oldlen = *((uint32_t *)p - 1);
    uint32_t minsize = (oldlen < newLen) ? oldlen : newLen;
    void *newblock = uc_my_mallocExt(newLen);
    if (newblock == NULL)
        return NULL;
    memmove(newblock, p, minsize);
    uc_my_freeExt(p);
    return newblock;
}
