#ifndef _MM_H
#define _MM_H

#include <core/system.h>
//#include <mm/buddy.h>

#if ARCH==X86
#include <arch/x86/include/mm.h>
#endif

extern int debug_kmalloc;

#ifdef DEBUG_KMALLOC
#define kmalloc(s) ((debug_kmalloc? printk("%s [%d]: kmalloc(%d)\n", __func__, __LINE__, (s)): 0), (kmalloc)((s)))
#define kfree(s) ((debug_kmalloc? printk("%s [%d]: kfree(%p)\n", __func__,  __LINE__, (s)): 0), (kfree)((s)))
#endif

void *(kmalloc)(size_t);
void (kfree)(void*);

#define LOWER_PAGE_BOUNDARY(ptr) ((ptr) & ~PAGE_MASK)
#define UPPER_PAGE_BOUNDARY(ptr) (((ptr) + PAGE_MASK) & ~PAGE_MASK)

void    mm_setup();
int     mm_map(paddr_t paddr, vaddr_t vaddr, size_t size, int flags);
void    mm_unmap(vaddr_t addr, size_t size);
void    mm_unmap_full(vaddr_t vaddr, size_t size);
void    mm_page_fault(vaddr_t vaddr);

struct page {
    int refs;
} __packed;

extern struct page pages[];

#endif /* !_MM_H */
