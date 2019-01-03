#ifndef _KVMEM_H
#define _KVMEM_H

void *kmalloc(size_t);
void kfree(void *);
extern int debug_kmalloc;

#ifdef DEBUG_KMALLOC
#define kmalloc(s) ((debug_kmalloc? printk("%s [%d]: kmalloc(%d)\n", __func__, __LINE__, (s)): 0), (kmalloc)((s)))
#define kfree(s) ((debug_kmalloc? printk("%s [%d]: kfree(%p)\n", __func__,  __LINE__, (s)): 0), (kfree)((s)))
#endif


#endif /* ! _KVMEM_H */
