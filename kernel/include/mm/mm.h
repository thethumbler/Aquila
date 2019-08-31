#ifndef _MM_MM_H
#define _MM_MM_H

#include <core/system.h>

#define PAGE_ALIGN(ptr) ((ptr) & ~PAGE_MASK)
#define PAGE_ROUND(ptr) (((ptr) + PAGE_MASK) & ~PAGE_MASK)

#include <boot/boot.h>

#define PF_PRESENT  0x001
#define PF_READ     0x002
#define PF_WRITE    0x004
#define PF_EXEC     0x008
#define PF_USER     0x010

void mm_setup(struct boot *boot);

struct vm_page *mm_page(paddr_t paddr);
struct vm_page *mm_page_alloc(void);
void   mm_page_incref(paddr_t paddr);
void   mm_page_decref(paddr_t paddr);
size_t mm_page_ref(paddr_t paddr);
void mm_page_dealloc(paddr_t paddr);

int  mm_page_map(struct pmap *pmap, vaddr_t vaddr, paddr_t paddr, int flags);
int  mm_map(struct pmap *pmap, paddr_t paddr, vaddr_t vaddr, size_t size, int flags);
void mm_unmap(struct pmap *pmap, vaddr_t addr, size_t size);
void mm_unmap_full(struct pmap *pmap, vaddr_t vaddr, size_t size);

void mm_page_fault(vaddr_t vaddr, int flags);

#endif /* !_MM_MM_H */
