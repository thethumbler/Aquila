/*
 *                 Intel 32-Bit Paging Mode Handler
 *
 *  References:
 *      [1] - Intel(R) 64 and IA-32 Architectures Software Developers Manual
 *          - Volume 3, System Programming Guide
 *          - Chapter 4, Paging
 *
 *  RAM is mapped from 0xC0000000 (ARCH_DMAP_BASE, the direct map, 768 MiB
 *  at most), page tables and pages are reached there. Every pmap has its
 *  own page directory; the kernel's half (from KERNEL_PDE) has all its
 *  page tables made at boot and shared by the directories, so the
 *  kernel's mappings are the same in every address space and never change
 *  in a directory. Switching is loading cr3.
 */

#include <core/arch.h>
#include <core/panic.h>
#include <core/printk.h>
#include <core/string.h>
#include <core/system.h>
#include <cpu/cpu.h>

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

/* the first entry of the kernel's half, 0xC0000000 */
#define KERNEL_PDE  768

static struct pmap k_pmap;
static struct pmap *cur_pmap = NULL;

static inline uint32_t *table(paddr_t paddr)
{
    return PHYS2VIRT(paddr);
}

static paddr_t table_alloc(void)
{
    struct vm_page *vm_page = mm_page_alloc();

    if (!vm_page)
        panic("could not allocate a page table");

    memset(table(vm_page->paddr), 0, PAGE_SIZE);
    return vm_page->paddr;
}

static void table_free(paddr_t paddr)
{
    buddy_free(BUDDY_ZONE_NORMAL, paddr, PAGE_SIZE);
}

/* the entry of the page table that maps `va' in `pmap', its table made
 * if `make', else NULL if there is none */
static uint32_t *pte_get(struct pmap *pmap, vaddr_t va, int make)
{
    uint32_t *pde = &table(pmap->map)[VDIR(va)];

    if (!(*pde & PG_PRESENT)) {
        if (!make)
            return NULL;

        *pde = table_alloc() | PG_PRESENT | PG_WRITE |
            (VDIR(va) < KERNEL_PDE ? PG_USER : 0);
    }

    return &table(PG_ADDR(*pde))[VTBL(va)];
}

static inline void tlb_invalidate_page(vaddr_t va)
{
    asm volatile("invlpg (%0)" :: "r"(va) : "memory");
}

struct pmap *pmap_switch(struct pmap *pmap)
{
    if (!pmap)
        panic("pmap?");

    struct pmap *ret = cur_pmap;

    if (pmap != cur_pmap) {
        write_cr3(pmap->map);
        cur_pmap = pmap;
    }

    return ret;
}

void pmap_init(void)
{
    /* the boot directory (arch/i386/boot/init.c) becomes the kernel's: it
     * maps the direct map, and the same at 0, which goes */
    k_pmap.map = read_cr3() & ~PAGE_MASK;
    k_pmap.ref = 1;
    kvm_space.pmap = &k_pmap;
    cur_pmap = &k_pmap;

    uint32_t *dir = table(k_pmap.map);

    for (int i = 0; i < KERNEL_PDE; ++i)
        dir[i] = 0;

    /* every table of the kernel's half, before there is any other
     * directory */
    for (int i = KERNEL_PDE; i < 1024; ++i)
        if (!(dir[i] & PG_PRESENT))
            dir[i] = table_alloc() | PG_PRESENT | PG_WRITE;

    tlb_flush();
}

static struct pmap *pmap_alloc(void)
{
    return kmalloc(sizeof(struct pmap), &M_PMAP, 0);
}

static void pmap_release(struct pmap *pmap)
{
    if (pmap == cur_pmap)
        pmap_switch(&k_pmap);

    if (pmap->map)
        table_free(pmap->map);

    kfree(pmap);
}

struct pmap *pmap_create(void)
{
    struct pmap *pmap = pmap_alloc();

    if (pmap == NULL)
        return NULL;

    memset(pmap, 0, sizeof(struct pmap));

    pmap->map = table_alloc();
    pmap->ref = 1;

    /* the kernel's half, the same in every directory */
    uint32_t *dir = table(pmap->map), *kdir = table(k_pmap.map);

    for (int i = KERNEL_PDE; i < 1024; ++i)
        dir[i] = kdir[i];

    return pmap;
}

void pmap_incref(struct pmap *pmap)
{
    pmap->ref++;
}

void pmap_decref(struct pmap *pmap)
{
    if (--pmap->ref == 0)
        pmap_release(pmap);
}

static uint32_t pte_flags(uint32_t flags)
{
    uint32_t pte = PG_PRESENT;

    if (flags & (VM_KW | VM_UW))
        pte |= PG_WRITE;

    if (flags & VM_URWX)
        pte |= PG_USER;

    /* device memory */
    if (flags & VM_NOCACHE)
        pte |= PG_PCD | PG_PWT;

    return pte;
}

int pmap_add(struct pmap *pmap, vaddr_t va, paddr_t pa, uint32_t flags)
{
    if ((va & PAGE_MASK) || (pa & PAGE_MASK))
        return -EINVAL;

    uint32_t *pte = pte_get(pmap, va, 1);
    *pte = pa | pte_flags(flags);

    if (pmap == cur_pmap || pmap == &k_pmap)
        tlb_invalidate_page(va);

    return 0;
}

void pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva)
{
    if ((sva & PAGE_MASK) || (eva & PAGE_MASK))
        return;

    for (; sva < eva; sva += PAGE_SIZE) {
        uint32_t *pte = pte_get(pmap, sva, 0);

        if (pte && (*pte & PG_PRESENT)) {
            *pte = 0;

            if (pmap == cur_pmap || pmap == &k_pmap)
                tlb_invalidate_page(sva);
        }
    }
}

/* the user half's tables go; the pages they map lose the reference the
 * page fault handler gave them (mm_page_incref) */
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

    if (pmap == cur_pmap)
        tlb_flush();
}

void pmap_protect(struct pmap *pmap, vaddr_t sva, vaddr_t eva, uint32_t prot)
{
    if (sva & PAGE_MASK)
        return;

    for (; sva < eva; sva += PAGE_SIZE) {
        uint32_t *pte = pte_get(pmap, sva, 0);

        if (pte && (*pte & PG_PRESENT)) {
            *pte = PG_ADDR(*pte) | pte_flags(prot);

            if (pmap == cur_pmap || pmap == &k_pmap)
                tlb_invalidate_page(sva);
        }
    }
}

void pmap_copy(struct pmap *dst_map, struct pmap *src_map, vaddr_t dst_addr, size_t len,
 vaddr_t src_addr)
{
    return;
}

void pmap_update(struct pmap *pmap)
{
    return;
}

void pmap_page_copy(paddr_t src, paddr_t dst)
{
    memcpy(PHYS2VIRT(dst), PHYS2VIRT(src), PAGE_SIZE);
}

void pmap_page_protect(struct vm_page *pg, uint32_t flags)
{
    return;
}

int pmap_clear_modify(struct vm_page *pg)
{
    return -1;
}

int pmap_clear_reference(struct vm_page *pg)
{
    return -1;
}

int pmap_is_modified(struct vm_page *pg)
{
    return -1;
}

int pmap_is_referenced(struct vm_page *pg)
{
    return -1;
}

paddr_t arch_page_get_mapping(struct pmap *pmap, vaddr_t vaddr)
{
    uint32_t *pte = pte_get(pmap, vaddr, 0);

    return pte && (*pte & PG_PRESENT) ? PG_ADDR(*pte) : 0;
}

int arch_page_unmap(vaddr_t vaddr)
{
    if (vaddr & PAGE_MASK)
        return -EINVAL;

    pmap_remove(cur_pmap, vaddr, vaddr + PAGE_SIZE);
    return 0;
}

int arch_mm_page_fault(vaddr_t vaddr, int err)
{
    int flags = 0;

    flags |= err & 0x01? PF_PRESENT : 0;
    flags |= err & 0x02? PF_WRITE   : PF_READ;
    flags |= err & 0x04? PF_USER    : 0;
    flags |= err & 0x10? PF_EXEC    : 0;

    return mm_page_fault(vaddr, flags);
}

int pmap_page_read(paddr_t paddr, off_t off, size_t size, void *buf)
{
    if (off < 0 || off >= PAGE_SIZE)
        return -EINVAL;

    memcpy(buf, (char *) PHYS2VIRT(paddr) + off, MIN(size, PAGE_SIZE - (size_t) off));
    return 0;
}

int pmap_page_write(paddr_t paddr, off_t off, size_t size, void *buf)
{
    if (off < 0 || off >= PAGE_SIZE)
        return -EINVAL;

    memcpy((char *) PHYS2VIRT(paddr) + off, buf, MIN(size, PAGE_SIZE - (size_t) off));
    return 0;
}

/* where the window has room for the memory past the direct map */
static vaddr_t phys_window_next = ARCH_PHYS_WINDOW_BASE;

static void *phys_window_map(uintptr_t paddr, size_t size, uint32_t flags)
{
    paddr_t start = PAGE_ALIGN(paddr);
    size_t  len   = PAGE_ROUND(paddr + size) - start;

    if (phys_window_next + len > ARCH_PHYS_WINDOW_BASE + ARCH_PHYS_WINDOW_SIZE)
        panic("no room to map memory past the direct map");

    vaddr_t vaddr = phys_window_next;
    phys_window_next += len;

    for (size_t off = 0; off < len; off += PAGE_SIZE)
        pmap_add(&k_pmap, vaddr + off, start + off, flags);

    return (void *) (vaddr + (paddr - start));
}

void *arch_phys_map(uintptr_t paddr, size_t size)
{
    if (paddr + size <= ARCH_DMAP_SIZE)
        return PHYS2VIRT(paddr);

    return phys_window_map(paddr, size, VM_KRW);
}

/* a device's registers, uncached */
void *arch_mmio_map(uintptr_t paddr, size_t size)
{
    return phys_window_map(paddr, size, VM_KRW | VM_NOCACHE);
}
