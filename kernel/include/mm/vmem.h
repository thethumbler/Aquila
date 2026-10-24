#ifndef _MM_VMEM_H
#define _MM_VMEM_H

/*
 * vmem allocates ranges of addresses (or of anything numbered), as
 * Bonwick and Adams describe it in "Magazines and Vmem" (2001), and as
 * Solaris, NetBSD and FreeBSD have it, without its caches.
 *
 * An arena is a range cut into segments, in address order, some free and
 * some allocated. The free segments are on lists by size: list i has
 * those of 2^i to 2^(i+1) - 1 quanta. A request for n quanta takes the
 * first segment of the first non-empty list whose segments are all big
 * enough (an "instant fit"), and what's left of that segment stays free.
 * The allocated segments are in a hash table by their start, which is all
 * that freeing one needs. A freed segment merges with the free segments
 * next to it.
 */

#include <core/system.h>

struct malloc_type;

/** A segment of an arena: a range that is either free or allocated. */
struct vmem_seg {
    uintptr_t base;
    size_t    size;                     /* in bytes */
    int       free;

    struct vmem_seg *prev, *next;       /* the arena's segments, in address order */
    struct vmem_seg *lprev, *lnext;     /* its free list, or its hash chain */

    /* what the allocation is, for its user (kvmem: kmalloc()'s object) */
    struct malloc_type *type;
    size_t bytes;
};

#define VMEM_LISTS  (sizeof(size_t) * 8)
#define VMEM_HASH   256

/** An arena: a range, and its segments. */
struct vmem {
    const char *name;
    size_t quantum;                     /* every size is a multiple of it */

    struct vmem_seg *segs;              /* the first segment, in address order */
    struct vmem_seg *free[VMEM_LISTS];  /* the free segments, by size */
    struct vmem_seg *hash[VMEM_HASH];   /* the allocated segments, by start */

    size_t size;                        /* the arena's size */
    size_t inuse;                       /* how much of it is allocated */
};

/* Sets up an arena: [base, base + size), all free. */
int vmem_init(struct vmem *vm, const char *name, uintptr_t base, size_t size, size_t quantum);

/* Allocates a segment of `size` bytes, rounded up to quanta. */
struct vmem_seg *vmem_alloc(struct vmem *vm, size_t size);

/* Finds the allocated segment that starts at `base`. */
struct vmem_seg *vmem_find(struct vmem *vm, uintptr_t base);

/* Frees an allocated segment. */
void vmem_free(struct vmem *vm, struct vmem_seg *seg);

#endif /* ! _MM_VMEM_H */
