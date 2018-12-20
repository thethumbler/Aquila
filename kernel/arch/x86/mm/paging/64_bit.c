#include <core/system.h>
#include <core/panic.h>
#include <core/printk.h>
#include <core/arch.h>
#include <cpu/cpu.h>
#include <ds/queue.h>
#include <mm/buddy.h>
#include <mm/vm.h>
#include <arch/x86/include/proc.h> /* XXX */
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>

#include "64_bit.h"

static volatile __dirtbl_t    *bsp_lvl4 = NULL;
static volatile __directory_t *bsp_lvl3 = NULL;
static volatile __table_t     *bsp_lvl2 = NULL;

static volatile __page_t last_page_table[512] __aligned(PAGE_SIZE) = {0};

#define MOUNT_ADDR  ((void *) 0xFFFF80003FFFF000)

#define LVL4            ((__dirtbl_t *)   (0xFFFFFFFFFFFFF000ULL))
#define LVL3(i)         ((__directory_t *)(0xFFFFFFFFFFE00000ULL + 0x1000ULL * (i)))
#define LVL2(i, j)      ((__table_t *)    (0xFFFFFFFFC0000000ULL + 512ULL * 0x1000 * (i) + 0x1000ULL * (j)))
#define LVL1(i, j, k)   ((__page_t *)     (0xFFFFFF8000000000ULL + 512ULL * 512ULL * 0x1000 * (i) + 512ULL * 0x1000 * (j) + 0x1000ULL * (k)))

#define VADDR(i, j, k, l)    ((uintptr_t)      ((512UL * 512UL * 512UL * (i) + 512UL * 512UL * (j) + 512UL * (k) + (l)) * 0x1000UL))

static inline void tlb_invalidate_page(uintptr_t virt)
{
    asm ("invlpg (%%rax)"::"a"(virt));
}

/* ================== Frame Helpers ================== */

static inline uintptr_t frame_mount(uintptr_t paddr)
{
    if (!paddr)
        return (uintptr_t) last_page_table[511].raw & ~PAGE_MASK;

    if (paddr & PAGE_MASK)
        panic("Mount must be on page (4K) boundary");

    uintptr_t prev = (uintptr_t) last_page_table[511].raw & ~PAGE_MASK;

    __page_t page;
    page.raw = paddr;

    page.present = 1;
    page.write = 1;

    last_page_table[511] = page;
    tlb_invalidate_page((uintptr_t) MOUNT_ADDR);

    return prev;
} 

#include "frame_utils.h"

/* ================== PDPT Helpers ================== */

static inline int pdpt_map(paddr_t paddr, size_t idx, int flags)
{
    if (idx > 511)
        return -EINVAL;

    __dirtbl_t dirtbl = {.raw = paddr};

    dirtbl.present = 1;
    dirtbl.write   = !!(flags & (VM_KW | VM_UW));
    dirtbl.user    = !!(flags & (VM_URWX));

    LVL4[idx] = dirtbl;

    //tlb_flush();

    return 0;
}

/* ================== PD Helpers ================== */

static inline int pd_map(paddr_t paddr, size_t pdpt_idx, size_t pd_idx, int flags)
{
    if (pdpt_idx > 511 || pd_idx > 511)
        return -EINVAL;

    __directory_t dir = {.raw = paddr};

    dir.present = 1;
    dir.write   = !!(flags & (VM_KW | VM_UW));
    dir.user    = !!(flags & (VM_URWX));

    LVL3(pdpt_idx)[pd_idx] = dir;

    //tlb_flush();

    return 0;
}

/* ================== Table Helpers ================== */

static inline int table_map(paddr_t paddr, size_t pdpt_idx, size_t pd_idx, size_t dir_idx, int flags)
{
    if (pdpt_idx > 511 || pd_idx > 511 || dir_idx > 511)
        return -EINVAL;

    __table_t table = {.raw = paddr};

    table.present = 1;
    table.write   = !!(flags & (VM_KW | VM_UW));
    table.user    = !!(flags & (VM_URWX));

    LVL2(pdpt_idx, pd_idx)[dir_idx] = table;

    //tlb_flush();

    return 0;
}

/* ================== Page Helpers ================== */

static inline int __page_map(paddr_t paddr, size_t pml4, size_t dirtbl, size_t dir, size_t table, int flags)
{
    /* Sanity checking */
    if (pml4 > 511 || dirtbl > 511 || dir > 511 || table > 511) {
        return -EINVAL;
    }

    __page_t page = {.raw = paddr};

    page.present = 1;
    page.write   = !!(flags & (VM_KW | VM_UW));
    page.user    = !!(flags & (VM_URWX));

    /* Check if PDPT is present */
    if (!LVL4[pml4].present) {
        paddr_t paddr = frame_get();
        pdpt_map(paddr, pml4, flags);
    }

    pages[GET_PHYS_ADDR(&LVL4[pml4])/PAGE_SIZE].refs++;

    /* Check if PD is present */
    if (!LVL3(pml4)[dirtbl].present) {
        paddr_t paddr = frame_get();
        pd_map(paddr, pml4, dirtbl, flags);
    }

    pages[GET_PHYS_ADDR(&LVL3(pml4)[dirtbl])/PAGE_SIZE].refs++;

    /* Check if table is present */
    if (!LVL2(pml4, dirtbl)[dir].present) {
        paddr_t paddr = frame_get();
        table_map(paddr, pml4, dirtbl, dir, flags);
    }

    pages[GET_PHYS_ADDR(&LVL2(pml4, dirtbl)[dir])/PAGE_SIZE].refs++;

    LVL1(pml4, dirtbl, dir)[table] = page;

    return 0;
}

static inline void __page_unmap(vaddr_t vaddr)
{
    if (vaddr & PAGE_MASK)
        return;

    __virtaddr_t virtaddr = {.raw = vaddr};
    size_t pml4      = virtaddr.pml4;
    size_t dirtbl    = virtaddr.dirtbl;
    size_t directory = virtaddr.directory;
    size_t table     = virtaddr.table;

    if (LVL4[pml4].present &&
        LVL3(pml4)[dirtbl].present &&
        LVL2(pml4, dirtbl)[directory].present) {

        /* Dereference page */
        LVL1(pml4, dirtbl, directory)[table].raw = 0;

#if 0
        /* Decrement references */
        paddr_t table_phy = GET_PHYS_ADDR(&LVL2(pml4, dirtbl)[directory]);
        pages[table_phy/PAGE_SIZE].refs--;
        if (!pages[table_phy/PAGE_SIZE].refs) {
            frame_release(table_phy);
            LVL2(pml4, dirtbl)[directory].present = 0;
        }

        paddr_t directory_phy = GET_PHYS_ADDR(&LVL3(pml4)[dirtbl]);
        pages[directory_phy/PAGE_SIZE].refs--;
        if (!pages[directory_phy/PAGE_SIZE].refs) {
            frame_release(directory_phy);
            LVL3(pml4)[dirtbl].present = 0;
        }

        paddr_t dirtbl_phy = GET_PHYS_ADDR(&LVL4[pml4]);
        pages[directory_phy/PAGE_SIZE].refs--;
        if (!pages[directory_phy/PAGE_SIZE].refs) {
            frame_release(directory_phy);
            LVL4[pml4].present = 0;
        }
#endif

        tlb_invalidate_page(vaddr);
    }
}


/* =================== XXX =============== */
#include "page_utils.h"


/* =================== Arch MM Interface =============== */

int arch_page_map(paddr_t paddr, vaddr_t vaddr, int flags)
{
    if (vaddr & PAGE_MASK)
        return -EINVAL;

    __virtaddr_t virtaddr = {.raw = vaddr};

    size_t pml4   = virtaddr.pml4;
    size_t dirtbl = virtaddr.dirtbl;
    size_t dir    = virtaddr.directory;
    size_t table  = virtaddr.table;

    __page_map(paddr, pml4, dirtbl, dir, table, flags);
    tlb_invalidate_page(vaddr);

    return 0;
}

int arch_page_unmap(vaddr_t vaddr)
{
    /* XXX */
    if (vaddr >= 0xFFFFD00000000000)
        printk("arch_page_unmap(vaddr=%p)\n", vaddr);

    if (vaddr & PAGE_MASK)
        return -EINVAL;

    __page_unmap(vaddr);

    return 0;
}

static inline __page_t *__page_get_mapping(uintptr_t addr)
{
    __virtaddr_t virt = (__virtaddr_t){.raw = addr};

    if (LVL4[virt.pml4].present &&
        LVL3(virt.pml4)[virt.dirtbl].present &&
        LVL2(virt.pml4, virt.dirtbl)[virt.directory].present &&
        LVL1(virt.pml4, virt.dirtbl, virt.directory)[virt.table].present) {

        __page_t *page = &LVL1(virt.pml4, virt.dirtbl, virt.directory)[virt.table];

        if (page->present)
            return page;
    }

    return NULL;
}

paddr_t arch_page_get_mapping(vaddr_t vaddr)
{
    __page_t *page = __page_get_mapping(vaddr);

    if (page)
        return GET_PHYS_ADDR(page);

    return 0;
}

void arch_mm_page_fault(vaddr_t vaddr)
{
    //printk("arch_mm_page_fault(vaddr=%p)\n", vaddr);

    vaddr_t page_addr = vaddr & ~PAGE_MASK;
    __page_t *page = __page_get_mapping(page_addr);

    if (page) { /* Page is mapped */
        paddr_t paddr = GET_PHYS_ADDR(page);
        size_t page_idx = paddr/PAGE_SIZE;

        if (pages[page_idx].refs == 1) {
            page->write = 1;
            tlb_invalidate_page(vaddr);
        } else {
            pages[page_idx].refs--;
            page->present = 0;
            mm_map(0, page_addr, PAGE_SIZE, VM_URWX);   /* FIXME */ 
            copy_physical_to_virtual((void *) page_addr, (void *) paddr, PAGE_SIZE);
        }

        return;
    }

    mm_page_fault(vaddr);
}

paddr_t arch_get_frame()
{
    return frame_get();
}

void arch_release_frame(paddr_t paddr)
{
    frame_release(paddr);
}

static paddr_t cur_map = 0;
void arch_switch_mapping(paddr_t map)
{
    if (cur_map == map) return;

    if (cur_map) { /* Store current directory mapping in old_dir */
        copy_virtual_to_physical((void *) cur_map, (void *) bsp_lvl4, 255 * 8);
    }
    copy_physical_to_virtual((void *) bsp_lvl4, (void *) map, 255 * 8);

    cur_map = map;
    tlb_flush();
}

void arch_mm_fork(paddr_t base, paddr_t fork)
{
    //printk("arch_mm_fork(base=%p, fork=%p)\n", base, fork);
    arch_switch_mapping(fork);

    uintptr_t old_mount = frame_mount(base);
    volatile __dirtbl_t *dirtbl = (__dirtbl_t *) MOUNT_ADDR;

    for (int i = 0; i < 255; ++i) {
        if (dirtbl[i].present) {
            /* Allocate new directory table for mapping */
            paddr_t dirtbl_phy = frame_get();
            pdpt_map(dirtbl_phy, i, VM_URWX);

            uintptr_t _mnt = frame_mount(GET_PHYS_ADDR(&dirtbl[i]));
            volatile __directory_t *dir = MOUNT_ADDR;
            
            for (int j = 0; j < 512; ++j) {
                if (dir[j].present) {
                    /* Allocate new directory for mapping */
                    paddr_t dir_phy = frame_get();
                    pd_map(dir_phy, i, j, VM_URWX);

                    uintptr_t _mnt = frame_mount(GET_PHYS_ADDR(&dir[j]));
                    volatile __table_t *tbl = MOUNT_ADDR;

                    for (int k = 0; k < 512; ++k) {
                        if (tbl[k].present) {
                            /* Allocate new table for mapping */
                            paddr_t tbl_phy = frame_get();
                            table_map(tbl_phy, i, j, k, VM_URWX);

                            uintptr_t _mnt = frame_mount(GET_PHYS_ADDR(&tbl[k]));
                            volatile __page_t *pg = MOUNT_ADDR;

                            for (int l = 0; l < 512; ++l) {
                                if (pg[l].present) {
#if 0
                                    if (pg[l].write) pg[l].write = 0;
                                    LVL1(i, j, k)[l].raw = pg[l].raw;
                                    pages[GET_PHYS_ADDR(&pg[l])/PAGE_SIZE].refs++;
#else
                                    paddr_t pg_phy = frame_get();
                                    LVL1(i, j, k)[l].raw   = pg_phy;
                                    LVL1(i, j, k)[l].write = pg[l].write;
                                    LVL1(i, j, k)[l].user  = pg[l].user;
                                    vaddr_t vaddr = (vaddr_t) VADDR(i, j, k, l);
                                    tlb_invalidate_page(vaddr);
                                    copy_physical_to_virtual((void *) vaddr, (void *) (pg[l].raw & ~PAGE_MASK), PAGE_SIZE);

                                    pages[pg_phy/PAGE_SIZE].refs++;
#endif
                                    pages[tbl_phy/PAGE_SIZE].refs++;
                                    pages[dir_phy/PAGE_SIZE].refs++;
                                    pages[dirtbl_phy/PAGE_SIZE].refs++;
                                }
                            }
                            frame_mount(_mnt);
                        }
                    }
                    frame_mount(_mnt);
                }
            }
            frame_mount(_mnt);
        }
    }

    tlb_flush();
    frame_mount(old_mount);
    arch_switch_mapping(base);
}

void setup_64_bit_paging()
{
    printk("x86: Setting up 64-bit paging\n");

    uintptr_t __cur_pml4 = read_cr3() & ~PAGE_MASK;
    bsp_lvl4 = (__dirtbl_t *)    VMA(__cur_pml4);
    bsp_lvl3 = (__directory_t *) VMA((uintptr_t) GET_PHYS_ADDR(&bsp_lvl4[0]));
    bsp_lvl2 = (__table_t *)     VMA((uintptr_t) GET_PHYS_ADDR(&bsp_lvl3[0]));

    /* Recursive map paging structure */
    bsp_lvl4[511].raw     = __cur_pml4; //LMA((uintptr_t) bsp_lvl4);
    bsp_lvl4[511].write   = 1;
    bsp_lvl4[511].present = 1;

    /* Map last page table */
    bsp_lvl2[511].raw     = LMA((uintptr_t) last_page_table);
    bsp_lvl2[511].write   = 1;
    bsp_lvl2[511].present = 1;

    /* Unmap lower half */
    bsp_lvl4[0].raw = 0;

    tlb_flush();
}
