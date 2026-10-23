#ifndef _AARCH64_MM_PMAP_H
#define _AARCH64_MM_PMAP_H

/*
 * An address space's page tables: its user half's (TTBR0_EL1's top
 * table). The kernel's half is one set of tables, TTBR1_EL1's, the same
 * whatever process runs (mm/pmap.c).
 */
struct pmap {
    paddr_t map;
    size_t  ref;
};

/* the TLB's translations of [sva, eva) of `pmap', gone; `leave': it's
 * going away (one CPU: it isn't loaded anymore) */
void tlb_shootdown(struct pmap *pmap, vaddr_t sva, vaddr_t eva, int leave);
void pmap_loaded(struct pmap *from, struct pmap *to);

#include_next <mm/pmap.h>

#endif /* ! _AARCH64_MM_PMAP_H */
