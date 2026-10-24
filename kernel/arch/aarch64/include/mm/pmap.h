#ifndef _AARCH64_MM_PMAP_H
#define _AARCH64_MM_PMAP_H

/*
 * An address space's page tables: those of its user half (TTBR0_EL1's top
 * table). The kernel's half is one set of tables, in TTBR1_EL1, the same
 * whatever process runs (mm/pmap.c).
 */
struct pmap {
    paddr_t map;
    size_t  ref;
};

/* tlb_shootdown() and pmap_loaded(), as on x86 (arch/i386/include/mm/pmap.h
 * documents them). With one CPU, only this CPU's TLB is flushed. */
void tlb_shootdown(struct pmap *pmap, vaddr_t sva, vaddr_t eva, int leave);
void pmap_loaded(struct pmap *from, struct pmap *to);

#include_next <mm/pmap.h>

#endif /* ! _AARCH64_MM_PMAP_H */
