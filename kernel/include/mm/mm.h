#ifndef _MM_H
#define _MM_H

#include <core/system.h>

#if ARCH==X86
#include <arch/x86/include/mm.h>
#endif

/* Physical Memory Manager interface */

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

typedef struct
{
    int     (*map)(uintptr_t addr, size_t size, int flags);
    int     (*map_to)(uintptr_t phys, uintptr_t virt, size_t size, int flags);
    void    (*unmap)(uintptr_t addr, size_t size);
    void    (*unmap_full)(uintptr_t addr, size_t size);
#if 0
    void*   (*memcpypp)(void *phys_dest, void *phys_src, size_t n); /* Phys to Phys memcpy */
#endif
    void*   (*memcpypv)(void *virt_dest, void *phys_src, size_t n); /* Phys to Virt memcpy */
    void*   (*memcpyvp)(void *phys_dest, void *virt_src, size_t n); /* Virt to Phys memcpy */
    void*   (*memcpypp)(uintptr_t phys_dest, uintptr_t phys_src, size_t n); /* Phys to Phys memcpy */
    void    (*switch_mapping)(uintptr_t structue);
    void    (*copy_fork_mapping)(uintptr_t base, uintptr_t fork);
    void    (*handle_page_fault)(uintptr_t addr);
} pmman_t;


struct paging
{
    size_t  size;
    int     (*map)  (uintptr_t addr, size_t nr, int flags);
    void    (*unmap)(uintptr_t addr, size_t nr);
    void*   (*mount)(uintptr_t addr);
} __packed;

extern struct paging paging[NR_PAGE_SIZE];
extern pmman_t pmman;

extern uintptr_t buddy_alloc(size_t);
extern void buddy_free(uintptr_t, size_t);
extern void buddy_dump();
extern void buddy_set_unusable(uintptr_t, size_t);

extern void pmm_lazy_alloc(uintptr_t addr);

extern int debug_kmalloc;

#ifdef DEBUG_KMALLOC
#define kmalloc(s) ((debug_kmalloc? printk("%s [%d]: kmalloc(%d)\n", __func__, __LINE__, (s)): 0), (kmalloc)((s)))
#define kfree(s) ((debug_kmalloc? printk("%s [%d]: kfree(%p)\n", __func__,  __LINE__, (s)): 0), (kfree)((s)))
#endif

void *(kmalloc)(size_t);
void (kfree)(void*);

#endif /* !_MM_H */
