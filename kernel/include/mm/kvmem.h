#ifndef _MM_KVMEM_H
#define _MM_KVMEM_H

/* The kernel's memory allocator (mm/kvmem.c). Objects of up to SLAB_MAX
 * bytes come from slabs (mm/slab.c), in sizes that are powers of two from
 * SLAB_MIN. */
#define SLAB_MIN        16
#define SLAB_MAX        1024
#define SLAB_CLASSES    7

struct slab;

/** A malloc type: what an allocation is for, which /proc/kvmem counts by. */
struct malloc_type {
    const char *name;
    const char *desc;
    size_t nr;              /* how many objects it has */
    size_t total;           /* their size, in bytes */
    struct qnode *qnode;    /* its node on the list of malloc types */

    /* its slabs with free objects, per size class */
    struct slab *slabs[SLAB_CLASSES];
};

/* A kmalloc() flag: zero the object. */
#define M_ZERO  0x0001

/* Declares, or defines, a malloc type. */
#define MALLOC_DECLARE(type) extern struct malloc_type (type)
#define MALLOC_DEFINE(type, _name, _desc) struct malloc_type (type) = {.name = (_name), .desc = (_desc)}

MALLOC_DECLARE(M_BUFFER);
MALLOC_DECLARE(M_RINGBUF);
MALLOC_DECLARE(M_QUEUE);
MALLOC_DECLARE(M_QNODE);
MALLOC_DECLARE(M_HASHMAP);
MALLOC_DECLARE(M_HASHMAP_NODE);

/* Allocates `size` bytes of `type` (zeroed with M_ZERO), or returns NULL
 * if there's no memory; frees what kmalloc() gave. */
void *kmalloc(size_t size, struct malloc_type *type, int flags);
void kfree(void *);

#endif /* ! _MM_KVMEM_H */
