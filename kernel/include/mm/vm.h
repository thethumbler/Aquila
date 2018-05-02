#ifndef _VM_H
#define _VM_H

#include <mm/mm.h>
#include <fs/vfs.h>
#include <ds/queue.h>

struct vmr {    /* Virtual Memory Region */
    uintptr_t base;
    size_t    size;
    int       flags;

    off_t     off;  /* Offset in file */
    struct inode *inode;

    struct queue_node *qnode;
};

void kvmem_setup();
int vm_map(uintptr_t phys_addr, struct vmr *vmr);
void vm_unmap(struct vmr *vmr);

#endif  /* ! _VM_H */
