#ifndef _VM_H
#define _VM_H

#include <core/system.h>

struct vmr;

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
#define VM_SHARED     0x0200  /* Shared mapping */

#define VM_KRW  (VM_KR|VM_KW) /* Kernel Read/Write */
#define VM_KRX  (VM_KR|VM_KX) /* Kernel Read/eXecute */
#define VM_KWX  (VM_KW|VM_KX) /* Kernel Write/eXecute */
#define VM_KRWX (VM_KR|VM_KW|VM_KX)  /* Kernel Read/Write/eXecute */
#define VM_URW  (VM_UR|VM_UW) /* User Read/Write */
#define VM_URX  (VM_UR|VM_UX) /* User Read/eXecute */
#define VM_UWX  (VM_UW|VM_UX) /* User Write/eXecute */
#define VM_URWX (VM_UR|VM_UW|VM_UX)  /* User Read/Write/eXecute */

/* Virtual Memory Region */
struct vmr {
    /* Physical Address, 0 means anywhere */
    paddr_t   paddr;

    uintptr_t base;
    size_t    size;
    int       flags;

    /* Offset in file */
    off_t     off;
    struct inode *inode;

    struct qnode *qnode;
};

void kvmem_setup(void);

int  vm_map(struct vmr *vmr);
void vm_unmap(struct vmr *vmr);
void vm_unmap_full(struct vmr *vmr);
int  vm_vmr_insert(struct queue *queue, struct vmr *vmr);

#endif  /* ! _VM_H */
