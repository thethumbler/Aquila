/** \file
 * i386: page tables, with 32-bit paging (two levels, 4 KiB pages).
 *
 * RAM is mapped from 0xC0000000 (`ARCH_DMAP_BASE`, the direct map, 768
 * MiB at most), and page tables and pages are reached there. Every
 * address space has its own page directory. The page tables of the
 * kernel's half (from `KERNEL_PDE`) are all made at boot and shared by
 * every directory, so the kernel's mappings are the same in every address
 * space, and a directory's kernel entries never change. Switching address
 * spaces is loading CR3.
 *
 * References:
 *      [1] - Intel(R) 64 and IA-32 Architectures Software Developer's
 *            Manual, Volume 3 (System Programming Guide), Chapter 4
 *            (Paging)
 */

#include <core/arch.h>
#include <core/panic.h>
#include <core/printk.h>
#include <core/string.h>
#include <core/system.h>
#include <cpu/cpu.h>
#include <sys/cpu.h>

#include <mm/pmap.h>
#include <mm/buddy.h>
#include <mm/mm.h>
#include <mm/vm.h>

MALLOC_DEFINE(M_PMAP, "pmap", "physical memory map structure");

#define PG_PRESENT  0x001
#define PG_WRITE    0x002
#define PG_USER     0x004
#define PG_PWT      0x008   /* write-through */
#define PG_PCD      0x010   /* cache disabled */

#define PG_ADDR(e)  ((e) & ~(uint32_t) PAGE_MASK)

#define VDIR(va)    (((va) >> 22) & 0x3FF)
#define VTBL(va)    (((va) >> 12) & 0x3FF)

/* the directory's first entry for the kernel's half, at 0xC0000000 */
#define KERNEL_PDE  768

static struct pmap k_pmap;

/* the running CPU's page tables, which its CR3 has */
#define cur_pmap (curcpu()->arch.pmap)

/** Returns a page table's kernel address, in the direct map. */
static inline uint32_t *table(paddr_t paddr)
{
    return PHYS2VIRT(paddr);
}

/** Allocates an empty page table. Returns its physical address, or 0 if
 * there is no memory for it. */
static paddr_t table_alloc(void)
{
    struct vm_page *vm_page = mm_page_alloc();

    if (!vm_page)
        return 0;

    memset(table(vm_page->paddr), 0, PAGE_SIZE);
    return vm_page->paddr;
}

/** Allocates a page table that the kernel can't boot without: it panics
 * if there is no memory. */
static paddr_t table_alloc_boot(void)
{
    paddr_t paddr = table_alloc();

    if (!paddr)
        panic("no memory for the kernel's page tables");

    return paddr;
}

/** Frees a page table. */
static void table_free(paddr_t paddr)
{
    mm_page_dealloc(paddr);
}

/** Returns the page table entry that maps `va` in `pmap`. With `make`, the
 * page table is made if there's none (NULL if there's no memory for it);
 * without it, NULL if there's no page table. */
static uint32_t *pte_get(struct pmap *pmap, vaddr_t va, int make)
{
    uint32_t *pde = &table(pmap->map)[VDIR(va)];

    if (!(*pde & PG_PRESENT)) {
        paddr_t t;

        if (!make || !(t = table_alloc()))
            return NULL;

        *pde = t | PG_PRESENT | PG_WRITE | (VDIR(va) < KERNEL_PDE ? PG_USER : 0);
    }

    return &table(PG_ADDR(*pde))[VTBL(va)];
}

/*
 * Switches the running CPU to an address space's page tables.
 */
struct pmap *pmap_switch(struct pmap *pmap)
{
    if (!pmap)
        panic("pmap?");

    struct pmap *ret = cur_pmap;

    if (pmap != cur_pmap) {
        pmap_loaded(cur_pmap, pmap);
        write_cr3(pmap->map);
        cur_pmap = pmap;
    }

    return ret;
}

/*
 * Takes over the boot page tables as the kernel's, at boot, and makes
 * every page table of the kernel's half, so that those entries never
 * change and every address space can share them.
 */
void pmap_init(void)
{
    /* The boot directory (boot/init.c) becomes the kernel's. It maps the
     * direct map, and the same memory at 0, which is removed. */
    k_pmap.map = read_cr3() & ~PAGE_MASK;
    k_pmap.ref = 1;
    kvm_space.pmap = &k_pmap;
    pmap_loaded(NULL, &k_pmap);
    cur_pmap = &k_pmap;

    uint32_t *dir = table(k_pmap.map);

    for (int i = 0; i < KERNEL_PDE; ++i)
        dir[i] = 0;

    /* make every page table of the kernel's half now, before there is
     * any other directory to share them */
    for (int i = KERNEL_PDE; i < 1024; ++i)
        if (!(dir[i] & PG_PRESENT))
            dir[i] = table_alloc_boot() | PG_PRESENT | PG_WRITE;

    tlb_flush();
}

/** Allocates a `struct pmap`. */
static struct pmap *pmap_alloc(void)
{
    return kmalloc(sizeof(struct pmap), &M_PMAP, 0);
}

/** Frees an address space's page directory, once no CPU has it loaded. */
static void pmap_release(struct pmap *pmap)
{
    /* make sure that no CPU has it loaded anymore */
    tlb_shootdown(pmap, 0, 0, 1);

    if (pmap->map)
        table_free(pmap->map);

    kfree(pmap);
}

/*
 * Makes the page tables of a new address space: an empty user half, and
 * the kernel's half, which every address space shares.
 */
struct pmap *pmap_create(void)
{
    struct pmap *pmap = pmap_alloc();

    if (pmap == NULL)
        return NULL;

    memset(pmap, 0, sizeof(struct pmap));

    if (!(pmap->map = table_alloc())) {
        kfree(pmap);
        return NULL;
    }

    pmap->ref = 1;

    /* the kernel's half, the same in every directory */
    uint32_t *dir = table(pmap->map), *kdir = table(k_pmap.map);

    for (int i = KERNEL_PDE; i < 1024; ++i)
        dir[i] = kdir[i];

    return pmap;
}

/*
 * Adds a reference to an address space's page tables.
 */
void pmap_incref(struct pmap *pmap)
{
    pmap->ref++;
}

/*
 * Drops a reference to an address space's page tables, freeing them with
 * the last one.
 */
void pmap_decref(struct pmap *pmap)
{
    if (--pmap->ref == 0)
        pmap_release(pmap);
}

/** Returns the page table entry bits for the `VM_*` flags `flags`. */
static uint32_t pte_flags(uint32_t flags)
{
    uint32_t pte = PG_PRESENT;

    if (flags & (VM_KW | VM_UW))
        pte |= PG_WRITE;

    if (flags & VM_URWX)
        pte |= PG_USER;

    /* device memory: uncached */
    if (flags & VM_NOCACHE)
        pte |= PG_PCD | PG_PWT;

    return pte;
}

/*
 * Maps a page in an address space, replacing what was mapped there.
 */
int pmap_add(struct pmap *pmap, vaddr_t va, paddr_t pa, uint32_t flags)
{
    if ((va & PAGE_MASK) || (pa & PAGE_MASK))
        return -EINVAL;

    uint32_t *pte = pte_get(pmap, va, 1);

    /* there was no memory for its page table */
    if (!pte)
        return -ENOMEM;

    uint32_t old = *pte;
    *pte = pa | pte_flags(flags);

    /* another page was mapped there: flush it from the TLBs */
    if (old & PG_PRESENT)
        tlb_shootdown(pmap, va, va + PAGE_SIZE, 0);

    return 0;
}

/*
 * Unmaps the pages of a range of an address space, and flushes them from
 * the TLBs that may have them.
 */
void pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva)
{
    if ((sva & PAGE_MASK) || (eva & PAGE_MASK))
        return;

    vaddr_t first = eva, last = sva;

    for (vaddr_t va = sva; va < eva; va += PAGE_SIZE) {
        uint32_t *pte = pte_get(pmap, va, 0);

        if (pte && (*pte & PG_PRESENT)) {
            *pte = 0;
            first = MIN(first, va);
            last = va + PAGE_SIZE;
        }
    }

    if (first < last)
        tlb_shootdown(pmap, first, last, 0);
}

/*
 * Frees the page tables of an address space's user half. The pages they
 * map lose the reference that the page fault handler gave them
 * (`mm_page_incref()`).
 */
void pmap_remove_all(struct pmap *pmap)
{
    uint32_t *dir = table(pmap->map);

    for (int i = 0; i < KERNEL_PDE; ++i) {
        if (!(dir[i] & PG_PRESENT))
            continue;

        uint32_t *t = table(PG_ADDR(dir[i]));

        for (int j = 0; j < 1024; ++j)
            if (t[j] & PG_PRESENT)
                mm_page_decref(PG_ADDR(t[j]));

        table_free(PG_ADDR(dir[i]));
        dir[i] = 0;
    }

    tlb_shootdown(pmap, 0, USER_STACK, 0);
}

/*
 * Changes the permissions of the mapped pages of a range of an address
 * space.
 */
void pmap_protect(struct pmap *pmap, vaddr_t sva, vaddr_t eva, uint32_t prot)
{
    if (sva & PAGE_MASK)
        return;

    vaddr_t first = eva, last = sva;

    for (vaddr_t va = sva; va < eva; va += PAGE_SIZE) {
        uint32_t *pte = pte_get(pmap, va, 0);

        if (pte && (*pte & PG_PRESENT)) {
            *pte = PG_ADDR(*pte) | pte_flags(prot);
            first = MIN(first, va);
            last = va + PAGE_SIZE;
        }
    }

    if (first < last)
        tlb_shootdown(pmap, first, last, 0);
}

/** Does nothing: the generic `pmap_copy()` isn't used on x86. */
void pmap_copy(struct pmap *dst_map, struct pmap *src_map, vaddr_t dst_addr, size_t len,
 vaddr_t src_addr)
{
    return;
}

/**
 * Makes the changes to an address space's page tables take effect. It
 * does nothing on x86, where they take effect at once.
 *
 * @param pmap  the page tables
 */
void pmap_update(struct pmap *pmap)
{
    return;
}

/*
 * Copies a page of physical memory to another, through the direct map.
 */
void pmap_page_copy(paddr_t src, paddr_t dst)
{
    memcpy(PHYS2VIRT(dst), PHYS2VIRT(src), PAGE_SIZE);
}

/**
 * Not implemented on x86 (it does nothing), and nothing uses it.
 *
 * @param pg     the page
 * @param flags  the permissions
 */
void pmap_page_protect(struct vm_page *pg, uint32_t flags)
{
    return;
}

/**
 * Not implemented on x86, and nothing uses it.
 *
 * @param pg  the page
 * @return -1
 */
int pmap_clear_modify(struct vm_page *pg)
{
    return -1;
}

/**
 * Not implemented on x86, and nothing uses it.
 *
 * @param pg  the page
 * @return -1
 */
int pmap_clear_reference(struct vm_page *pg)
{
    return -1;
}

/**
 * Not implemented on x86, and nothing uses it.
 *
 * @param pg  the page
 * @return -1
 */
int pmap_is_modified(struct vm_page *pg)
{
    return -1;
}

/**
 * Not implemented on x86, and nothing uses it.
 *
 * @param pg  the page
 * @return -1
 */
int pmap_is_referenced(struct vm_page *pg)
{
    return -1;
}

/*
 * Finds the page mapped at a virtual address of an address space.
 */
paddr_t arch_page_get_mapping(struct pmap *pmap, vaddr_t vaddr)
{
    uint32_t *pte = pte_get(pmap, vaddr, 0);

    return pte && (*pte & PG_PRESENT) ? PG_ADDR(*pte) : 0;
}

/**
 * Unmaps a page of the running CPU's address space.
 *
 * @param vaddr  the page's virtual address, page-aligned
 * @return 0, or `-EINVAL` if it isn't page-aligned
 */
int arch_page_unmap(vaddr_t vaddr)
{
    if (vaddr & PAGE_MASK)
        return -EINVAL;

    pmap_remove(cur_pmap, vaddr, vaddr + PAGE_SIZE);
    return 0;
}

/*
 * Handles a page fault: turns the CPU's error code into the kernel's
 * `PF_*` flags, and calls `mm_page_fault()`.
 */
int arch_mm_page_fault(vaddr_t vaddr, int err)
{
    int flags = 0;

    flags |= err & 0x01? PF_PRESENT : 0;
    flags |= err & 0x02? PF_WRITE   : PF_READ;
    flags |= err & 0x04? PF_USER    : 0;
    flags |= err & 0x10? PF_EXEC    : 0;

    return mm_page_fault(vaddr, flags);
}

/*
 * Reads from a page of physical memory, through the direct map.
 */
int pmap_page_read(paddr_t paddr, off_t off, size_t size, void *buf)
{
    if (off < 0 || off >= PAGE_SIZE)
        return -EINVAL;

    memcpy(buf, (char *) PHYS2VIRT(paddr) + off, MIN(size, PAGE_SIZE - (size_t) off));
    return 0;
}

/*
 * Writes to a page of physical memory, through the direct map.
 */
int pmap_page_write(paddr_t paddr, off_t off, size_t size, void *buf)
{
    if (off < 0 || off >= PAGE_SIZE)
        return -EINVAL;

    memcpy((char *) PHYS2VIRT(paddr) + off, buf, MIN(size, PAGE_SIZE - (size_t) off));
    return 0;
}

/* where the window for the memory past the direct map has room next */
static vaddr_t phys_window_next = ARCH_PHYS_WINDOW_BASE;

/** Maps a range of physical memory in the window, with `flags`. Returns
 * its kernel address, or NULL if there's no room left in the window, or no
 * memory for a page table. The window's addresses are never reused. */
static void *phys_window_map(uintptr_t paddr, size_t size, uint32_t flags)
{
    paddr_t start = PAGE_ALIGN(paddr);
    size_t  len   = PAGE_ROUND(paddr + size) - start;

    if (phys_window_next + len > ARCH_PHYS_WINDOW_BASE + ARCH_PHYS_WINDOW_SIZE)
        return NULL;    /* no room left in the window */

    vaddr_t vaddr = phys_window_next;

    /* if there's no memory for a page table, unmap what was mapped */
    for (size_t off = 0; off < len; off += PAGE_SIZE) {
        if (pmap_add(&k_pmap, vaddr + off, start + off, flags)) {
            pmap_remove(&k_pmap, vaddr, vaddr + off);
            return NULL;
        }
    }

    phys_window_next += len;

    return (void *) (vaddr + (paddr - start));
}

/*
 * Returns a kernel address for a range of physical memory: its address
 * in the direct map if it's there, else a mapping made in the window.
 */
void *arch_phys_map(uintptr_t paddr, size_t size)
{
    if (paddr + size <= ARCH_DMAP_SIZE)
        return PHYS2VIRT(paddr);

    return phys_window_map(paddr, size, VM_KRW);
}

/*
 * Maps a device's registers in the kernel's address space, uncached.
 */
void *arch_mmio_map(uintptr_t paddr, size_t size)
{
    return phys_window_map(paddr, size, VM_KRW | VM_NOCACHE);
}
