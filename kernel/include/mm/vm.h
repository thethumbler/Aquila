#ifndef _VM_H
#define _VM_H

#include <mm/mm.h>
#include <fs/vfs.h>
#include <ds/queue.h>

#define VM_KR         0x0001  /* Kernel Read */
#define VM_KW         0x0002  /* Kernel Write */
#define VM_KX         0x0004  /* Kernel eXecute */
#define VM_UR         0x0008  /* User Read */
#define VM_UW         0x0010  /* User Write */
#define VM_UX         0x0020  /* User eXecute */
#define VM_NOCACHE    0x0040  /* Disable caching */
#define VM_FILE       0x0080  /* File backed */
#define VM_ZERO       0x0100  /* Zero fill */

#define VM_KRW  (VM_KR|VM_KW) /* Kernel Read/Write */
#define VM_KRX  (VM_KR|VM_KX) /* Kernel Read/eXecute */
#define VM_KWX  (VM_KW|VM_KX) /* Kernel Write/eXecute */
#define VM_KRWX (VM_KR|VM_KW|VM_KX)  /* Kernel Read/Write/eXecute */
#define VM_URW  (VM_UR|VM_UW) /* User Read/Write */
#define VM_URX  (VM_UR|VM_UX) /* User Read/eXecute */
#define VM_UWX  (VM_UW|VM_UX) /* User Write/eXecute */
#define VM_URWX (VM_UR|VM_UW|VM_UX)  /* User Read/Write/eXecute */

struct vmr {    /* Virtual Memory Region */
    paddr_t   paddr;    /* Physical Address, 0 means anywhere */
    uintptr_t base;
    size_t    size;
    int       flags;

    off_t     off;  /* Offset in file */
    struct inode *inode;

    struct queue_node *qnode;
};

void kvmem_setup();

int  vm_map(struct vmr *vmr);
void vm_unmap(struct vmr *vmr);

#endif  /* ! _VM_H */
