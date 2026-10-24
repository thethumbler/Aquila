#ifndef _MM_MM_H
#define _MM_MM_H

#include <core/system.h>

/* Physical memory, and page faults (mm/mm.c, mm/fault.c). */

/* An address rounded down, or up, to a page. */
#define PAGE_ALIGN(ptr) ((ptr) & ~PAGE_MASK)
#define PAGE_ROUND(ptr) (((ptr) + PAGE_MASK) & ~PAGE_MASK)

#include <boot/boot.h>

/* What a page fault is: a page was mapped there; a read, a write or an
 * instruction fetch; from user mode. */
#define PF_PRESENT  0x001
#define PF_READ     0x002
#define PF_WRITE    0x004
#define PF_EXEC     0x008
#define PF_USER     0x010

struct pmap;

/* Sets up physical memory at boot. */
void mm_setup(struct boot *boot);

/* Allocates and frees pages, and counts their references. */
struct vm_page *mm_page_alloc(void);
void   mm_page_incref(paddr_t paddr);
void   mm_page_decref(paddr_t paddr);
size_t mm_page_ref(paddr_t paddr);
void mm_page_dealloc(paddr_t paddr);

/* Maps and unmaps pages in an address space. */
int  mm_page_map(struct pmap *pmap, vaddr_t vaddr, paddr_t paddr, int flags);
int  mm_map(struct pmap *pmap, paddr_t paddr, vaddr_t vaddr, size_t size, int flags);
void mm_unmap_full(struct pmap *pmap, vaddr_t vaddr, size_t size);

/* Handles a page fault in the current process. */
int  mm_page_fault(vaddr_t vaddr, int flags);

#endif /* !_MM_MM_H */
