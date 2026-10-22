#ifndef _I386_MM_PMAP_H
#define _I386_MM_PMAP_H

struct pmap {
    paddr_t map;
    size_t  ref;
    volatile uint32_t cpus;     /* the CPUs it's loaded on (mm/tlb.c) */
};

/* mm/tlb.c: the TLBs of the CPUs that may have [sva, eva) of `pmap'
 * cached, flushed (every CPU's for the kernel's); `leave': they switch
 * to the kernel's tables instead (it's going away) */
void tlb_shootdown(struct pmap *pmap, vaddr_t sva, vaddr_t eva, int leave);
void pmap_loaded(struct pmap *from, struct pmap *to);
void x86_tlb_ipi(void);

#include_next <mm/pmap.h>

#endif /* ! _I386_MM_PMAP_H */
