#ifndef _MM_H
#define _MM_H

#include <core/system.h>

#define LOWER_PAGE_BOUNDARY(ptr) ((ptr) & ~PAGE_MASK)
#define UPPER_PAGE_BOUNDARY(ptr) (((ptr) + PAGE_MASK) & ~PAGE_MASK)

#include <boot/boot.h>

void mm_setup(struct boot *boot);
int  mm_map(paddr_t paddr, vaddr_t vaddr, size_t size, int flags);
void mm_unmap(vaddr_t addr, size_t size);
void mm_unmap_full(vaddr_t vaddr, size_t size);
void mm_page_fault(vaddr_t vaddr);

struct page {
    int refs;
};

extern struct page pages[];

#endif /* !_MM_H */
