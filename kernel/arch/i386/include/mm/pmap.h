#ifndef _I386_MM_PMAP_H
#define _I386_MM_PMAP_H

/** An address space's page tables, on x86. */
struct pmap {
    paddr_t map;                /* the top table's physical address (CR3) */
    size_t  ref;
    volatile uint32_t cpus;     /* the CPUs it's loaded on (mm/tlb.c) */
};

/**
 * Flushes a range of an address space from every TLB that may have it
 * cached, after its mappings changed (every CPU's, for the kernel's
 * half). Each architecture implements it, in its mm code.
 *
 * @param pmap   the page tables that changed
 * @param sva    the range's start
 * @param eva    its end
 * @param leave  instead of flushing, make the CPUs that have `pmap`
 *               loaded switch to the kernel's page tables, because it is
 *               going away
 */
void tlb_shootdown(struct pmap *pmap, vaddr_t sva, vaddr_t eva, int leave);

/**
 * Records that this CPU switched page tables, so that TLB flushes go to
 * the CPUs that may have a mapping cached.
 *
 * @param from  the page tables it had loaded, or NULL if none
 * @param to    the page tables it loaded
 */
void pmap_loaded(struct pmap *from, struct pmap *to);
void x86_tlb_ipi(void);

#include_next <mm/pmap.h>

#endif /* ! _I386_MM_PMAP_H */
