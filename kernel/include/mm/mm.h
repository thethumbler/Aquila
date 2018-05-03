#ifndef _MM_H
#define _MM_H

#include <core/system.h>
//#include <mm/buddy.h>

#if ARCH==X86
#include <arch/x86/include/mm.h>
#endif

/* Physical Memory Manager interface */

#if 0
typedef struct {
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

struct paging {
    size_t  size;
    int     (*map)  (uintptr_t addr, size_t nr, int flags);
    void    (*unmap)(uintptr_t addr, size_t nr);
    void*   (*mount)(uintptr_t addr);
} __packed;

//extern struct paging paging[NR_PAGE_SIZE];
//extern pmman_t pmman;
//extern void pmm_lazy_alloc(uintptr_t addr);
#endif

extern int debug_kmalloc;

#ifdef DEBUG_KMALLOC
#define kmalloc(s) ((debug_kmalloc? printk("%s [%d]: kmalloc(%d)\n", __func__, __LINE__, (s)): 0), (kmalloc)((s)))
#define kfree(s) ((debug_kmalloc? printk("%s [%d]: kfree(%p)\n", __func__,  __LINE__, (s)): 0), (kfree)((s)))
#endif

void *(kmalloc)(size_t);
void (kfree)(void*);


typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

#define LOWER_PAGE_BOUNDARY(ptr) ((ptr) & ~PAGE_MASK)
#define UPPER_PAGE_BOUNDARY(ptr) (((ptr) + PAGE_MASK) & ~PAGE_MASK)

void    mm_setup();
int     mm_map(paddr_t paddr, vaddr_t vaddr, size_t size, int flags);
void    mm_unmap(vaddr_t addr, size_t size);
void    mm_page_fault(vaddr_t vaddr);

struct page {
    int refs;
} __packed;

extern struct page pages[];

#endif /* !_MM_H */
