#ifndef _HEAP_H
#define _HEAP_H

/* FIXME */
extern char *kernel_heap;
static inline void *heap_alloc(size_t size, size_t align)
{
    char *ret = (char *)((uintptr_t)(kernel_heap + align - 1) & (~(align - 1)));
    kernel_heap = ret + size;

    memset(ret, 0, size);   /* We always clear the allocated area */

    return ret;
}

#endif /* _HEAP_H */
