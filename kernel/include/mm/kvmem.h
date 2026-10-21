#ifndef _MM_KVMEM_H
#define _MM_KVMEM_H

/* kmalloc() sizes up to SLAB_MAX come from slabs (mm/slab.c): powers of
 * two from SLAB_MIN */
#define SLAB_MIN        16
#define SLAB_MAX        1024
#define SLAB_CLASSES    7

struct slab;

struct malloc_type {
    const char *name;
    const char *desc;
    size_t nr;
    size_t total;
    struct qnode *qnode;

    /* its slabs with free objects, per size */
    struct slab *slabs[SLAB_CLASSES];
};

#define M_ZERO  0x0001

#define MALLOC_DECLARE(type) extern struct malloc_type (type)
#define MALLOC_DEFINE(type, _name, _desc) struct malloc_type (type) = {.name = (_name), .desc = (_desc)}

MALLOC_DECLARE(M_BUFFER);
MALLOC_DECLARE(M_RINGBUF);
MALLOC_DECLARE(M_QUEUE);
MALLOC_DECLARE(M_QNODE);
MALLOC_DECLARE(M_HASHMAP);
MALLOC_DECLARE(M_HASHMAP_NODE);

/* `size' bytes of `type' (M_ZERO: zeroed), NULL if there's no memory */
void *kmalloc(size_t size, struct malloc_type *type, int flags);
void kfree(void *);

#endif /* ! _MM_KVMEM_H */
