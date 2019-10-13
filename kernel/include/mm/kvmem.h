#ifndef _MM_KVMEM_H
#define _MM_KVMEM_H

struct malloc_type {
    const char *name;
    const char *desc;
    size_t nr;
    size_t total;
    struct qnode *qnode;
};

#define M_ZERO  0x0001

#define MALLOC_DECLARE(type) extern struct malloc_type (type)
#define MALLOC_DEFINE(type, name, desc) struct malloc_type (type) = {(name), (desc), 0, 0, NULL}

MALLOC_DECLARE(M_BUFFER);
MALLOC_DECLARE(M_RINGBUF);
MALLOC_DECLARE(M_QUEUE);
MALLOC_DECLARE(M_QNODE);
MALLOC_DECLARE(M_HASHMAP);
MALLOC_DECLARE(M_HASHMAP_NODE);

void *kmalloc(size_t, struct malloc_type *type, int flags);
void kfree(void *);
extern int debug_kmalloc;

#ifdef DEBUG_KMALLOC
#define kmalloc(s) ((debug_kmalloc? printk("%s [%d]: kmalloc(%d)\n", __func__, __LINE__, (s)): 0), (kmalloc)((s)))
#define kfree(s) ((debug_kmalloc? printk("%s [%d]: kfree(%p)\n", __func__,  __LINE__, (s)): 0), (kfree)((s)))
#endif

#endif /* ! _MM_KVMEM_H */
