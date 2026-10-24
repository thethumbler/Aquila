#ifndef _MM_PMAP_H
#define _MM_PMAP_H

/*
 * The page tables, which each architecture implements (arch/ARCH/mm/). A
 * pmap is an address space's page tables. `pmap_add()` maps a page,
 * `pmap_remove()` unmaps a range, and `pmap_protect()` changes a range's
 * permissions. The `pmap_page_*()` functions reach a physical page
 * through the kernel's mapping.
 */

/** Takes over the boot page tables as the kernel's, at boot, and makes
 * the page tables of the kernel's half, which every address space shares. */
void pmap_init(void);

/**
 * Switches this CPU to an address space's page tables.
 *
 * @param pmap  the page tables to switch to
 * @return the page tables the CPU had before
 */
struct pmap *pmap_switch(struct pmap *pmap);

/**
 * Makes the page tables of a new address space: an empty user half, and
 * the kernel's half, which every address space shares.
 *
 * @return the new page tables, or NULL if there was no memory for them
 */
struct pmap *pmap_create(void);

/**
 * Adds a reference to an address space's page tables.
 *
 * @param pmap  the page tables
 */
void pmap_incref(struct pmap *pmap);

/**
 * Drops a reference to an address space's page tables, freeing them
 * with the last one.
 *
 * @param pmap  the page tables
 */
void pmap_decref(struct pmap *pmap);

/**
 * Maps a page in an address space, replacing what was mapped there.
 *
 * @param pmap   the page tables (an address in the kernel's half is
 *               mapped in every address space)
 * @param va     the virtual address, page-aligned
 * @param pa     the page's physical address, page-aligned
 * @param flags  the mapping's permissions and caching (`VM_*`)
 * @return 0, `-EINVAL` for an address that isn't page-aligned (or that
 *         can't be mapped page by page, in the direct map), or `-ENOMEM`
 *         if there was no memory for a page table
 */
int  pmap_add(struct pmap *pmap, vaddr_t va, paddr_t pa, uint32_t flags);

/**
 * Unmaps the pages of a range of an address space, and flushes them from
 * the TLBs that may have them. The pages lose no references.
 *
 * @param pmap  the page tables
 * @param sva   the range's start, page-aligned
 * @param eva   its end, page-aligned
 */
void pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva);

/**
 * Changes the permissions of the mapped pages of a range of an address
 * space. Pages that aren't mapped stay unmapped.
 *
 * @param pmap  the page tables
 * @param sva   the range's start, page-aligned
 * @param eva   its end
 * @param prot  the new permissions (`VM_*`)
 */
void pmap_protect(struct pmap *pmap, vaddr_t sva, vaddr_t eva, uint32_t prot);

/**
 * Copies a page of physical memory to another.
 *
 * @param src  the physical address of the page to copy
 * @param dst  the physical address of the page to copy it to
 */
void pmap_page_copy(paddr_t src, paddr_t dst);

/**
 * Unmaps the whole user half of an address space, and frees its page
 * tables. The pages it mapped lose the reference that the page fault
 * handler gave them (`mm_page_incref()`).
 *
 * @param pmap  the page tables
 */
void pmap_remove_all(struct pmap *pmap);

/**
 * Reads from a page of physical memory, through the kernel's mapping.
 *
 * @param paddr  the page's physical address
 * @param off    the offset to read from, in the page
 * @param size   how much to read, at most what's left of the page
 * @param buf    where to put it
 * @return 0, or `-EINVAL` if `off` isn't in the page
 */
int pmap_page_read(paddr_t paddr, off_t off, size_t size, void *buf);

/**
 * Writes to a page of physical memory, through the kernel's mapping.
 *
 * @param paddr  the page's physical address
 * @param off    the offset to write at, in the page
 * @param size   how much to write, at most what's left of the page
 * @param buf    what to write
 * @return 0, or `-EINVAL` if `off` isn't in the page
 */
int pmap_page_write(paddr_t paddr, off_t off, size_t size, void *buf);

#endif /* _MM_PMAP_H */
