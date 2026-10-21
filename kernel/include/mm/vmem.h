#ifndef _MM_VMEM_H
#define _MM_VMEM_H

/*
 * vmem: an allocator of ranges (of addresses, or of anything numbered),
 * as Bonwick and Adams describe it ("Magazines and Vmem", 2001) and
 * Solaris, NetBSD and FreeBSD have it, without its caches
 *
 * An arena is a range cut into segments, in address order: free ones,
 * and allocated ones. The free ones are on lists by size, list i has
 * those of [2^i, 2^(i+1)) quanta: a request of n quanta takes the first
 * segment of the first non-empty list from the one whose sizes are all
 * at least n (instant fit), the rest of it free again. The allocated
 * ones are in a hash by their base, which is all freeing needs; a freed
 * segment merges with the free ones next to it (their boundary tags).
 */

#include <core/system.h>

struct malloc_type;

struct vmem_seg {
    uintptr_t base;
    size_t    size;                     /* bytes */
    int       free;

    struct vmem_seg *prev, *next;       /* the arena's, in address order */
    struct vmem_seg *lprev, *lnext;     /* its free list, or hash chain */

    /* what the allocation is, for its user (kvmem: kmalloc's) */
    struct malloc_type *type;
    size_t bytes;
};

#define VMEM_LISTS  (sizeof(size_t) * 8)
#define VMEM_HASH   256

struct vmem {
    const char *name;
    size_t quantum;                     /* sizes are multiples of it */

    struct vmem_seg *segs;              /* the first, in address order */
    struct vmem_seg *free[VMEM_LISTS];  /* free, by size */
    struct vmem_seg *hash[VMEM_HASH];   /* allocated, by base */

    size_t size;                        /* of the arena */
    size_t inuse;                       /* allocated */
};

/* the arena [base, base + size): 0, -ENOMEM */
int vmem_init(struct vmem *vm, const char *name, uintptr_t base, size_t size, size_t quantum);

/* a segment of `size' bytes (rounded up to quanta), NULL if there's no
 * room (or no memory for its record) */
struct vmem_seg *vmem_alloc(struct vmem *vm, size_t size);

/* the allocated segment that starts at `base', NULL if none does */
struct vmem_seg *vmem_find(struct vmem *vm, uintptr_t base);

/* an allocated segment is free again */
void vmem_free(struct vmem *vm, struct vmem_seg *seg);

#endif /* ! _MM_VMEM_H */
