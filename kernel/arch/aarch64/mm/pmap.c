/** \file
 *             aarch64: 4-level page tables, 4 KiB pages
 *
 *  References:
 *      [1] - Arm Architecture Reference Manual for A-profile, D8 (The
 *            AArch64 Virtual Memory System Architecture)
 *
 *  Two halves, each with its own top table: user space is TTBR0_EL1's,
 *  one table a pmap (pmap->map); the kernel's is TTBR1_EL1's, one table
 *  for all, set at boot and never switched. So the kernel's mappings are
 *  the same in every address space without copying anything, and
 *  switching address spaces is loading TTBR0_EL1.
 *
 *  All RAM is mapped at ARCH_DMAP_BASE (the direct map: the boot tables'
 *  1 GiB blocks, boot/head.S), page tables and pages are reached there.
 *  No ASIDs: a switch flushes the TLB.
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

/* descriptors: valid; at levels 0 to 2 a table, or (level 1, 2) a
 * block; at level 3 a page */
#define D_VALID     (1ULL << 0)
#define D_TABLE     (1ULL << 1)     /* not a block (levels 0-2), a page (3) */
#define D_ATTR(i)   ((uint64_t) (i) << 2)   /* MAIR_EL1's: 0 normal, 1 device */
#define D_AP_EL0    (1ULL << 6)     /* user mode may access it */
#define D_AP_RO     (1ULL << 7)     /* read only */
#define D_SH_INNER  (3ULL << 8)
#define D_AF        (1ULL << 10)    /* accessed (no faults to set it) */
#define D_NG        (1ULL << 11)    /* not global: a process's */
#define D_PXN       (1ULL << 53)    /* the kernel doesn't execute it */
#define D_UXN       (1ULL << 54)    /* user mode doesn't execute it */

#define D_ADDR(e)   ((e) & 0x0000FFFFFFFFF000ULL)

/* the entry of `va' in its table of `level' (3: level 0, the top; 0:
 * level 3, the pages): 9 bits a level, as x86-64's */
#define IDX(va, level)  (((va) >> (12 + 9 * (level))) & 0x1FF)

#define KERNEL_HALF(va) ((va) >= ARCH_DMAP_BASE)

static struct pmap k_pmap;

/* the kernel half's top table (TTBR1_EL1's: head.S's boot_l0_hi) */
static paddr_t k_root;

/* the direct map's end: RAM, at least the boot tables' 4 GiB */
static paddr_t dmap_end = ARCH_BOOT_MAPPED;

/* the running CPU's (in TTBR0_EL1) */
#define cur_pmap (curcpu()->arch.pmap)

static inline uint64_t *table(paddr_t paddr)
{
    return PHYS2VIRT(paddr);
}

/** a page table, empty; 0 if there's no memory for it */
static paddr_t table_alloc(void)
{
    struct vm_page *vm_page = mm_page_alloc();

    if (!vm_page)
        return 0;

    memset(table(vm_page->paddr), 0, PAGE_SIZE);
    return vm_page->paddr;
}

static void table_free(paddr_t paddr)
{
    mm_page_dealloc(paddr);
}

/** the entry of the page table that maps `va' (in `pmap', or the
 * kernel's half), the tables on the way made if `make' (NULL if there's
 * no memory for one), else NULL if one is missing */
static uint64_t *pte_get(struct pmap *pmap, vaddr_t va, int make)
{
    uint64_t *t = table(KERNEL_HALF(va) ? k_root : pmap->map);

    for (int level = 3; level > 0; --level) {
        uint64_t *e = &t[IDX(va, level)];

        if (!(*e & D_VALID)) {
            paddr_t t_new;

            if (!make || !(t_new = table_alloc()))
                return NULL;

            *e = t_new | D_VALID | D_TABLE;
        }

        /* the direct map's blocks aren't the pmap's to change */
        if (!(*e & D_TABLE))
            return NULL;

        t = table(D_ADDR(*e));
    }

    return &t[IDX(va, 0)];
}

/** [sva, eva) of `pmap' out of the TLB, if it's loaded (one CPU) */
void tlb_shootdown(struct pmap *pmap, vaddr_t sva, vaddr_t eva, int leave)
{
    if (leave) {
        if (cur_pmap == pmap)
            pmap_switch(&k_pmap);
        return;
    }

    if (pmap != &k_pmap && pmap != cur_pmap)
        return;

    asm volatile("dsb ishst" ::: "memory");

    if (eva - sva > 32 * PAGE_SIZE) {
        asm volatile("tlbi vmalle1" ::: "memory");
    } else {
        for (vaddr_t va = sva; va < eva; va += PAGE_SIZE)
            asm volatile("tlbi vaae1, %0" :: "r"((va >> 12) & 0xFFFFFFFFFFFULL) : "memory");
    }

    asm volatile("dsb ish; isb" ::: "memory");
}

void pmap_loaded(struct pmap *from, struct pmap *to)
{
}

struct pmap *pmap_switch(struct pmap *pmap)
{
    if (!pmap)
        panic("pmap?");

    struct pmap *ret = cur_pmap;

    if (pmap != cur_pmap) {
        write_sysreg(ttbr0_el1, pmap->map);
        isb();
        tlb_flush();
        cur_pmap = pmap;
    }

    return ret;
}

/** the end of the RAM the boot loader found */
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
    extern char boot_l0_hi[], boot_l1[];

    /* the boot tables' kernel half becomes the kernel's */
    k_root = LMA((uintptr_t) boot_l0_hi);

    /* RAM past the boot tables' 4 GiB, in the direct map too: 1 GiB
     * blocks of normal memory, in the level 1 table they share */
    uint64_t *l1 = (uint64_t *) boot_l1;
    paddr_t end = (ram_end() + (1ULL << 30) - 1) & ~((1ULL << 30) - 1);

    for (paddr_t pa = ARCH_BOOT_MAPPED; pa < end && pa < (512ULL << 30); pa += 1ULL << 30)
        l1[pa >> 30] = pa | D_VALID | D_ATTR(0) | D_SH_INNER | D_AF;

    dmap_end = MAX(end, ARCH_BOOT_MAPPED);

    /* the kernel's own user half: none (the boot tables' identity map
     * goes with it) */
    if (!(k_pmap.map = table_alloc()))
        panic("no memory for the kernel's page tables");

    k_pmap.ref = 1;
    kvm_space.pmap = &k_pmap;

    cur_pmap = NULL;
    pmap_switch(&k_pmap);

    printk("aarch64: %ld MiB in the direct map\n", (uint64_t) (dmap_end >> 20));
}

static void pmap_release(struct pmap *pmap)
{
    /* not loaded anymore */
    tlb_shootdown(pmap, 0, 0, 1);

    if (pmap->map)
        table_free(pmap->map);

    kfree(pmap);
}

struct pmap *pmap_create(void)
{
    struct pmap *pmap = kmalloc(sizeof(struct pmap), &M_PMAP, M_ZERO);

    if (pmap == NULL)
        return NULL;

    /* the user half only: the kernel's is TTBR1_EL1's */
    if (!(pmap->map = table_alloc())) {
        kfree(pmap);
        return NULL;
    }

    pmap->ref = 1;

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
    uint64_t pte = D_VALID | D_TABLE | D_AF;
    int user = flags & VM_URWX, write = flags & (VM_KW | VM_UW);

    /* EL1 always has access; EL0's is AP[1]; read only, AP[2] */
    if (user)
        pte |= D_AP_EL0 | D_NG;
    if (!write)
        pte |= D_AP_RO;

    /* the kernel never runs user pages, user mode runs only what says so */
    if (user || !(flags & VM_KX))
        pte |= D_PXN;
    if (!(flags & VM_UX))
        pte |= D_UXN;

    /* device memory, or normal */
    pte |= flags & VM_NOCACHE ? D_ATTR(1) | D_PXN | D_UXN : D_ATTR(0) | D_SH_INNER;

    return pte;
}

int pmap_add(struct pmap *pmap, vaddr_t va, paddr_t pa, uint32_t flags)
{
    if ((va & PAGE_MASK) || (pa & PAGE_MASK))
        return -EINVAL;

    /* the direct map's blocks aren't changed page by page */
    if (va >= ARCH_DMAP_BASE && va < ARCH_DMAP_BASE + dmap_end)
        return -EINVAL;

    uint64_t *pte = pte_get(pmap, va, 1);

    /* no memory for a table on the way */
    if (!pte)
        return -ENOMEM;

    uint64_t old = *pte;
    *pte = pa | pte_flags(flags);

    /* another page was there */
    if (old & D_VALID)
        tlb_shootdown(KERNEL_HALF(va) ? &k_pmap : pmap, va, va + PAGE_SIZE, 0);
    else
        asm volatile("dsb ishst; isb" ::: "memory");

    return 0;
}

void pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva)
{
    if ((sva & PAGE_MASK) || (eva & PAGE_MASK))
        return;

    vaddr_t first = eva, last = sva;

    for (vaddr_t va = sva; va < eva; va += PAGE_SIZE) {
        uint64_t *pte = pte_get(pmap, va, 0);

        if (pte && (*pte & D_VALID)) {
            *pte = 0;
            first = MIN(first, va);
            last = va + PAGE_SIZE;
        }
    }

    if (first < last)
        tlb_shootdown(KERNEL_HALF(sva) ? &k_pmap : pmap, first, last, 0);
}

/** the tables of the user half, from `level' down; the pages they map
 * lose the reference the page fault handler gave them (mm_page_incref) */
static void tables_free(paddr_t paddr, int level)
{
    uint64_t *t = table(paddr);

    for (int i = 0; i < 512; ++i) {
        if (!(t[i] & D_VALID))
            continue;

        if (level > 1)
            tables_free(D_ADDR(t[i]), level - 1);
        else
            mm_page_decref(D_ADDR(t[i]));
    }

    table_free(paddr);
}

void pmap_remove_all(struct pmap *pmap)
{
    uint64_t *l0 = table(pmap->map);

    for (int i = 0; i < 512; ++i) {
        if (l0[i] & D_VALID) {
            tables_free(D_ADDR(l0[i]), 3);
            l0[i] = 0;
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

        if (pte && (*pte & D_VALID)) {
            *pte = D_ADDR(*pte) | pte_flags(prot);
            first = MIN(first, va);
            last = va + PAGE_SIZE;
        }
    }

    if (first < last)
        tlb_shootdown(KERNEL_HALF(sva) ? &k_pmap : pmap, first, last, 0);
}

void pmap_page_copy(paddr_t src, paddr_t dst)
{
    memcpy(PHYS2VIRT(dst), PHYS2VIRT(src), PAGE_SIZE);
}

paddr_t arch_page_get_mapping(struct pmap *pmap, vaddr_t vaddr)
{
    uint64_t *pte = pte_get(pmap, vaddr, 0);
    return pte && (*pte & D_VALID) ? D_ADDR(*pte) : 0;
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

/* where the window has room for devices' registers */
static vaddr_t mmio_next = ARCH_MMIO_BASE;

/** physical memory mapped in the window, with `flags'; NULL if there's
 * no room left in it, or no memory for a page table */
static void *window_map(uintptr_t paddr, size_t size, uint32_t flags)
{
    paddr_t start = PAGE_ALIGN(paddr);
    size_t  len   = PAGE_ROUND(paddr + size) - start;

    if (mmio_next + len > ARCH_MMIO_BASE + ARCH_MMIO_SIZE)
        return NULL;    /* no room left in the window */

    vaddr_t vaddr = mmio_next;

    /* no memory for a page table: what was mapped of it goes */
    for (size_t off = 0; off < len; off += PAGE_SIZE) {
        if (pmap_add(&k_pmap, vaddr + off, start + off, flags)) {
            pmap_remove(&k_pmap, vaddr, vaddr + off);
            return NULL;
        }
    }

    mmio_next += len;

    return (void *) (vaddr + (paddr - start));
}

/** physical memory: in the direct map if it's RAM, else mapped in the
 * window (memory past it) */
void *arch_phys_map(uintptr_t paddr, size_t size)
{
    if (paddr >= (1ULL << 30) && paddr + size <= dmap_end)
        return PHYS2VIRT(paddr);

    return window_map(paddr, size, VM_KRW);
}

/** a device's registers, uncached (device memory) */
void *arch_mmio_map(uintptr_t paddr, size_t size)
{
    return window_map(paddr, size, VM_KRW | VM_NOCACHE);
}

void arch_mm_setup(void)
{
    pmap_init();
}
