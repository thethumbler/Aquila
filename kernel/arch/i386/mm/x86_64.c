/*
 *                 x86-64 4-Level Paging
 *
 *  References:
 *      [1] - Intel(R) 64 and IA-32 Architectures Software Developers Manual
 *          - Volume 3, System Programming Guide
 *          - Chapter 4, Paging (4.5, 4-Level Paging)
 *
 *  All physical memory is mapped at ARCH_DMAP_BASE (the direct map), page
 *  tables and pages are reached there. Every pmap has its own PML4. The
 *  kernel's half (entries 256 to 511) has all its PDPTs made at boot and
 *  shared by the PML4s, so the kernel's mappings are the same in every
 *  address space and never change in a PML4. Switching is loading cr3.
 */

#include <core/arch.h>
#include <core/panic.h>
#include <core/printk.h>
#include <core/string.h>
#include <core/system.h>
#include <boot/boot.h>
#include <cpu/cpu.h>
#include <sys/cpu.h>

#include <mm/pmap.h>
#include <mm/buddy.h>
#include <mm/mm.h>
#include <mm/vm.h>

MALLOC_DEFINE(M_PMAP, "pmap", "physical memory map structure");

extern struct boot *__kboot;

#define PG_PRESENT  0x001
#define PG_WRITE    0x002
#define PG_USER     0x004
#define PG_PWT      0x008   /* write-through */
#define PG_PCD      0x010   /* cache disabled */
#define PG_LARGE    0x080   /* 2 MiB (in a PD), 1 GiB (in a PDPT) */
#define PG_GLOBAL   0x100

#define PG_ADDR(e)  ((e) & 0x000FFFFFFFFFF000ULL)

/* the entry of `va' in its table of `level' (0: PT, 1: PD, 2: PDPT, 3: PML4) */
#define IDX(va, level)  (((va) >> (12 + 9 * (level))) & 0x1FF)

#define KERNEL_PML4E    256
#define LARGE_SIZE      0x200000ULL

static struct pmap k_pmap;

/* the running CPU's (in its CR3) */
#define cur_pmap (curcpu()->arch.pmap)

static inline uint64_t *table(paddr_t paddr)
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

/* the entry of the page table that maps `va' in `pmap', the tables on the
 * way made if `make', else NULL if one is missing */
static uint64_t *pte_get(struct pmap *pmap, vaddr_t va, int make)
{
    uint64_t *t = table(pmap->map);
    uint64_t flags = PG_PRESENT | PG_WRITE | (IDX(va, 3) < KERNEL_PML4E ? PG_USER : 0);

    for (int level = 3; level > 0; --level) {
        uint64_t *e = &t[IDX(va, level)];

        if (!(*e & PG_PRESENT)) {
            if (!make)
                return NULL;

            *e = table_alloc() | flags;
        }

        /* the direct map's large pages aren't the pmap's to change */
        if (*e & PG_LARGE)
            return NULL;

        t = table(PG_ADDR(*e));
    }

    return &t[IDX(va, 0)];
}

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

/* the end of the RAM the boot loader found */
static paddr_t ram_end(void)
{
    paddr_t end = 0;

    for (int i = 0; i < __kboot->mmap_count; ++i)
        if (__kboot->mmap[i].type == MMAP_USABLE && __kboot->mmap[i].end > end)
            end = __kboot->mmap[i].end;

    return end;
}

void pmap_init(void)
{
    /* the boot tables (arch/i386/boot/x86_64_bootstrap.S) become the
     * kernel's: they map the kernel at -2 GiB and the first GiB of the
     * direct map (and itself, which goes) */
    k_pmap.map = read_cr3() & ~PAGE_MASK;
    k_pmap.ref = 1;
    kvm_space.pmap = &k_pmap;
    pmap_loaded(NULL, &k_pmap);
    cur_pmap = &k_pmap;

    uint64_t *pml4 = table(k_pmap.map);

    /* the rest of the RAM in the direct map, with 2 MiB pages */
    paddr_t end = (ram_end() + LARGE_SIZE - 1) & ~(LARGE_SIZE - 1);

    for (paddr_t pa = 0; pa < end; pa += LARGE_SIZE) {
        vaddr_t va = ARCH_DMAP_BASE + pa;
        uint64_t *t = pml4;

        for (int level = 3; level > 1; --level) {
            uint64_t *e = &t[IDX(va, level)];

            if (!(*e & PG_PRESENT))
                *e = table_alloc() | PG_PRESENT | PG_WRITE;

            t = table(PG_ADDR(*e));
        }

        t[IDX(va, 1)] = pa | PG_PRESENT | PG_WRITE | PG_LARGE | PG_GLOBAL;
    }

    /* every PDPT of the kernel's half, before there is any other PML4 */
    for (int i = KERNEL_PML4E; i < 512; ++i)
        if (!(pml4[i] & PG_PRESENT))
            pml4[i] = table_alloc() | PG_PRESENT | PG_WRITE;

    /* nothing at the bottom anymore: user space */
    pml4[0] = 0;
    tlb_flush();

    printk("x86_64: %ld MiB in the direct map\n", (uint64_t) (end >> 20));
}

static struct pmap *pmap_alloc(void)
{
    return kmalloc(sizeof(struct pmap), &M_PMAP, 0);
}

static void pmap_release(struct pmap *pmap)
{
    /* no CPU has it loaded anymore */
    tlb_shootdown(pmap, 0, 0, 1);

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

    /* the kernel's half, the same in every PML4 */
    uint64_t *pml4 = table(pmap->map), *kpml4 = table(k_pmap.map);

    for (int i = KERNEL_PML4E; i < 512; ++i)
        pml4[i] = kpml4[i];

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

static uint64_t pte_flags(uint32_t flags)
{
    uint64_t pte = PG_PRESENT;

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

    uint64_t *pte = pte_get(pmap, va, 1);

    if (!pte)
        return -EINVAL;

    uint64_t old = *pte;
    *pte = pa | pte_flags(flags);

    /* another page was there */
    if (old & PG_PRESENT)
        tlb_shootdown(pmap, va, va + PAGE_SIZE, 0);

    return 0;
}

void pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva)
{
    if ((sva & PAGE_MASK) || (eva & PAGE_MASK))
        return;

    vaddr_t first = eva, last = sva;

    for (vaddr_t va = sva; va < eva; va += PAGE_SIZE) {
        uint64_t *pte = pte_get(pmap, va, 0);

        if (pte && (*pte & PG_PRESENT)) {
            *pte = 0;
            first = MIN(first, va);
            last = va + PAGE_SIZE;
        }
    }

    if (first < last)
        tlb_shootdown(pmap, first, last, 0);
}

/* the tables of the user half, from `level' down; the pages they map
 * lose the reference the page fault handler gave them (mm_page_incref) */
static void tables_free(paddr_t paddr, int level)
{
    uint64_t *t = table(paddr);

    for (int i = 0; i < 512; ++i) {
        if (!(t[i] & PG_PRESENT))
            continue;

        if (level > 1)
            tables_free(PG_ADDR(t[i]), level - 1);
        else
            mm_page_decref(PG_ADDR(t[i]));
    }

    table_free(paddr);
}

void pmap_remove_all(struct pmap *pmap)
{
    uint64_t *pml4 = table(pmap->map);

    for (int i = 0; i < KERNEL_PML4E; ++i) {
        if (pml4[i] & PG_PRESENT) {
            tables_free(PG_ADDR(pml4[i]), 3);
            pml4[i] = 0;
        }
    }

    tlb_shootdown(pmap, 0, USER_STACK, 0);
}

void pmap_protect(struct pmap *pmap, vaddr_t sva, vaddr_t eva, uint32_t prot)
{
    if (sva & PAGE_MASK)
        return;

    vaddr_t first = eva, last = sva;

    for (vaddr_t va = sva; va < eva; va += PAGE_SIZE) {
        uint64_t *pte = pte_get(pmap, va, 0);

        if (pte && (*pte & PG_PRESENT)) {
            *pte = PG_ADDR(*pte) | pte_flags(prot);
            first = MIN(first, va);
            last = va + PAGE_SIZE;
        }
    }

    if (first < last)
        tlb_shootdown(pmap, first, last, 0);
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
    uint64_t *pte = pte_get(pmap, vaddr, 0);

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

/* all of the RAM is in the direct map (and what the boot loader left is
 * in RAM) */
void *arch_phys_map(uintptr_t paddr, size_t size)
{
    return PHYS2VIRT(paddr);
}

/* where the window has room for devices' registers */
static vaddr_t mmio_next = ARCH_MMIO_BASE;

/* a device's registers, uncached */
void *arch_mmio_map(uintptr_t paddr, size_t size)
{
    paddr_t start = PAGE_ALIGN(paddr);
    size_t  len   = PAGE_ROUND(paddr + size) - start;

    if (mmio_next + len > ARCH_MMIO_BASE + ARCH_MMIO_SIZE)
        panic("no room to map a device's registers");

    vaddr_t vaddr = mmio_next;
    mmio_next += len;

    for (size_t off = 0; off < len; off += PAGE_SIZE)
        pmap_add(&k_pmap, vaddr + off, start + off, VM_KRW | VM_NOCACHE);

    return (void *) (vaddr + (paddr - start));
}
