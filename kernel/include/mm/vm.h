#ifndef _VM_H
#define _VM_H

#include <core/system.h>

struct vm_space;
struct vm_entry;
struct vm_object;
struct vm_page;

#include <mm/mm.h>
#include <ds/queue.h>
#include <ds/hashmap.h>

struct vm_space {
    struct pmap  *pmap;
    struct queue  vm_entries;
};

#include <fs/vfs.h>

#define VM_KR         0x0001         /* Kernel Read */
#define VM_KW         0x0002         /* Kernel Write */
#define VM_KX         0x0004         /* Kernel eXecute */
#define VM_UR         0x0008         /* User Read */
#define VM_UW         0x0010         /* User Write */
#define VM_UX         0x0020         /* User eXecute */
#define VM_PERM       0x003F         /* Permissions Mask */
#define VM_NOCACHE    0x0040         /* Disable caching */
#define VM_FILE       0x0080         /* File backed */
#define VM_ZERO       0x0100         /* Zero fill */
#define VM_SHARED     0x0200         /* Shared mapping */

#define VM_KRW  (VM_KR|VM_KW)        /* Kernel Read/Write */
#define VM_KRX  (VM_KR|VM_KX)        /* Kernel Read/eXecute */
#define VM_KWX  (VM_KW|VM_KX)        /* Kernel Write/eXecute */
#define VM_KRWX (VM_KR|VM_KW|VM_KX)  /* Kernel Read/Write/eXecute */
#define VM_URW  (VM_UR|VM_UW)        /* User Read/Write */
#define VM_URX  (VM_UR|VM_UX)        /* User Read/eXecute */
#define VM_UWX  (VM_UW|VM_UX)        /* User Write/eXecute */
#define VM_URWX (VM_UR|VM_UW|VM_UX)  /* User Read/Write/eXecute */

/* Virtual Memory Region */
struct vm_entry {
    /* Physical Address, 0 means anywhere */
    paddr_t  paddr;

    vaddr_t  base;
    size_t   size;
    uint32_t flags;

    /* backening object */
    struct vm_object *vm_object;

    /* offset in object */
    off_t off;

    //struct inode *inode;
    struct qnode *qnode;
};

struct vm_object {
    struct hashmap *pages;
    struct vm_object *shadow;
    struct inode *inode;
};

struct vm_page {
    struct vm_object *object;
    off_t off;

    uint32_t flags;
    paddr_t  paddr;

    struct qnode *qnode;

    size_t   ref;
};

extern struct vm_page pages[];
extern struct vm_space kvm_space;

void kvmem_setup(void);

int  vm_fork(struct vm_space *parent, struct vm_space *child);
int  vm_map(struct vm_space *vm_space, struct vm_entry *vm_entry);
void vm_unmap(struct vm_space *vm_space, struct vm_entry *vm_entry);
void vm_unmap_full(struct vm_space *vm_space, struct vm_entry *vm_entry);
int  vm_entry_insert(struct vm_space *vm_space, struct vm_entry *vm_entry);
void vm_space_destroy(struct vm_space *vm_space);

struct vm_object *vm_object_inode(struct inode *inode);

MALLOC_DECLARE(M_VM_ENTRY);

#endif  /* ! _VM_H */
