#ifndef _MM_VM_H
#define _MM_VM_H

#include <core/system.h>
#include <core/printk.h>

struct vm_space;
struct vm_entry;
struct vm_object;
struct vm_page;

MALLOC_DECLARE(M_VM_ENTRY);
MALLOC_DECLARE(M_VM_OBJECT);
MALLOC_DECLARE(M_VM_ANON);
MALLOC_DECLARE(M_VM_AREF);

#include <ds/queue.h>

/** 
 * \ingroup mm
 * \brief virtual memory space
 *
 * This structure holds the entire mapping of a virtual memory 
 * space/view
 */
struct vm_space {
    /** physical memory mapper (arch-specific) */
    struct pmap  *pmap;

    /** virtual memory regions inside the vm space */
    struct queue  vm_entries;
};

#include <fs/vfs.h>
#include <mm/mm.h>
#include <ds/hashmap.h>

/* vm entry flags */
#define VM_KR         0x0001         /**< kernel read */
#define VM_KW         0x0002         /**< kernel write */
#define VM_KX         0x0004         /**< kernel execute */
#define VM_UR         0x0008         /**< user read */
#define VM_UW         0x0010         /**< user write */
#define VM_UX         0x0020         /**< user execute */
#define VM_PERM       0x003F         /**< permissions mask */
#define VM_NOCACHE    0x0040         /**< disable caching */
#define VM_SHARED     0x0080         /**< shared mapping */
#define VM_COPY       0x0100         /**< needs copy */

#define VM_KRW  (VM_KR|VM_KW)        /**< kernel read/write */
#define VM_KRX  (VM_KR|VM_KX)        /**< kernel read/execute */
#define VM_KWX  (VM_KW|VM_KX)        /**< kernel write/execute */
#define VM_KRWX (VM_KR|VM_KW|VM_KX)  /**< kernel read/write/execute */
#define VM_URW  (VM_UR|VM_UW)        /**< user read/write */
#define VM_URX  (VM_UR|VM_UX)        /**< user read/execute */
#define VM_UWX  (VM_UW|VM_UX)        /**< user write/execute */
#define VM_URWX (VM_UR|VM_UW|VM_UX)  /**< user read/write/execute */

/* object types */
#define VMOBJ_ZERO    0x0000         /**< zero fill */
#define VMOBJ_FILE    0x0001         /**< file backed */

/** 
 * \ingroup mm
 * \brief virtual memory region
 *
 * This structure represents a region in virtual memory
 * space with access permissions and backening objects
 */
struct vm_entry {
    /** Physical Address, 0 means anywhere - XXX */
    paddr_t  paddr;

    /** address of the vm entry inside the `vm_space` */
    vaddr_t base;

    /** size of the vm entry */
    size_t size;

    /** permissions flags */
    uint32_t flags;

    /** anon layer object */
    struct vm_anon *vm_anon;

    /** backening object */
    struct vm_object *vm_object;

    /** offset inside object */
    size_t off;

    /** the queue node this vm entry is stored in */
    struct qnode *qnode;
};

/**
 * \ingroup mm
 * \brief pager
 */
struct vm_pager {
    /** page in */
    struct vm_page *(*in)(struct vm_object *vm_object, size_t off);

    /** page out */
    int (*out)(struct vm_object *vm_object, size_t off);
};

/**
 * \ingroup mm
 * \brief anonymous memory object reference
 *
 * The `vm_aref` acts as a container of a `vm_page` with reference count
 * of anonymous memory regions (`vm_anon`) referencing/using that page.
 */
struct vm_aref {
    /** vm page associated with the aref */
    struct vm_page *vm_page;

    /** number of references to the aref */
    size_t ref;

    /** flags associated with this aref */
    uint32_t flags;
};

/**
 * \ingroup mm
 * \brief anonymous memory object
 */
struct vm_anon {
    /** hashmap of `vm_aref` structures loaded/contained in this anon */
    struct hashmap *arefs;

    /** number of `vm_entry` structures referencing this anon */
    size_t ref;

    /** flags associated with the anon */
    int flags;
};

/**
 * \ingroup mm
 * \brief cached object
 */
struct vm_object {
    /** `vm_page`s loaded/contained in the vm object */
    struct hashmap *pages;

    /** type of the object */
    int type;

    /** number of vm entries referencing this object */
    size_t ref;

    /** pager for the vm object */
    struct vm_pager *pager;

    /** pager private data */
    void *p;
};

/** 
 * \ingroup mm
 * \brief physical page
 */
struct vm_page {
    paddr_t  paddr; /**< physical address of the page */

    struct vm_object *vm_object; /**< the object this page belongs to */

    size_t off; /**< offset of page inside the object */

    size_t ref; /**< number of processes referencing this page */
};

extern struct vm_page pages[];
extern struct vm_space kvm_space;

void kvmem_setup(void);

/* mm/vmm.c XXX */
int  vm_map(struct vm_space *vm_space, struct vm_entry *vm_entry);
void vm_unmap(struct vm_space *vm_space, struct vm_entry *vm_entry);
void vm_unmap_full(struct vm_space *vm_space, struct vm_entry *vm_entry);

/* mm/vm_space.c */
int  vm_space_fork(struct vm_space *parent, struct vm_space *child);
void vm_space_destroy(struct vm_space *vm_space);
struct vm_entry *vm_space_find(struct vm_space *vm_space, vaddr_t vaddr);
int  vm_space_insert(struct vm_space *vm_space, struct vm_entry *vm_entry);

/* mm/vm_entry.c */
struct vm_entry *vm_entry_new(void);
void vm_entry_destroy(struct vm_entry *vm_entry);

/* mm/vm_anon.c */
struct vm_anon *vm_anon_new(void);
struct vm_anon *vm_anon_copy(struct vm_anon *vm_anon);
void vm_anon_incref(struct vm_anon *vm_anon);
void vm_anon_decref(struct vm_anon *vm_anon);
void vm_anon_destroy(struct vm_anon *vm_anon);

/* mm/vm_object.c */
struct vm_object *vm_object_inode(struct inode *inode);
struct vm_page *vm_object_page_get(struct vm_object *vm_object, size_t off);
void vm_object_page_insert(struct vm_object *vm_object, struct vm_page *vm_page);
void vm_object_incref(struct vm_object *vm_object);
void vm_object_decref(struct vm_object *vm_object);

#endif  /* ! _MM_VM_H */
