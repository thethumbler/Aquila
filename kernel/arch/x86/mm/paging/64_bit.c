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

static volatile __dirtbl_t    *bootstrap_processor_pml4 = NULL;
static volatile __directory_t *bootstrap_processor_pdpt = NULL;
static volatile __table_t     *bootstrap_processor_dir  = NULL;
static volatile __page_t last_page_table[512] __aligned(PAGE_SIZE) = {0};

#define MOUNT_ADDR  ((void *) 0xFFFF80003FFFF000)

#define PML4                 ((__dirtbl_t *)   (0xFFFFFFFFFFFFF000ULL))
#define PDPT(i)              ((__directory_t *)(0xFFFFFFFFFFE00000ULL + 0x1000ULL * (i)))
#define PAGE_DIR(i, j)       ((__table_t *)    (0xFFFFFFFFC0000000ULL + 512ULL * 0x1000 * (i) + 0x1000ULL * (j)))
#define PAGE_TBL(i, j, k)    ((__page_t *)     (0xFFFFFF8000000000ULL + 512ULL * 512ULL * 0x1000 * (i) + 512ULL * 0x1000 * (j) + 0x1000ULL * (k)))

static inline void tlb_invalidate_page(uintptr_t virt)
{
    asm ("invlpg (%%eax)"::"a"(virt));
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

static inline uintptr_t frame_get()
{
    uintptr_t frame = buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);

    if (!frame) {
        panic("Could not allocate frame");
    }

    uintptr_t old = frame_mount(frame);
    memset(MOUNT_ADDR, 0, PAGE_SIZE);
    frame_mount(old);

    return frame;
}

static inline uintptr_t frame_get_no_clr()
{
    uintptr_t frame = buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);

    if (!frame) {
        panic("Could not allocate frame");
    }

    return frame;
}

static void frame_release(uintptr_t i)
{
    buddy_free(BUDDY_ZONE_NORMAL, i, PAGE_SIZE);
}

/* ================== PDPT Helpers ================== */

static inline int pdpt_map(paddr_t paddr, size_t idx, int flags)
{
    if (idx > 511)
        return -EINVAL;

    __dirtbl_t dirtbl = {.raw = paddr};

    dirtbl.present = 1;
    dirtbl.write   = !!(flags & (VM_KW | VM_UW));
    dirtbl.user    = !!(flags & (VM_URWX));

    PML4[idx] = dirtbl;

    tlb_flush();

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

    PDPT(pdpt_idx)[pd_idx] = dir;

    tlb_flush();

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

    PAGE_DIR(pdpt_idx, pd_idx)[dir_idx] = table;

    tlb_flush();

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
    if (!PML4[pml4].present) {
        paddr_t paddr = frame_get();
        pdpt_map(paddr, pml4, flags);
        pages[paddr/PAGE_SIZE].refs++;
    }

    /* Check if PD is present */
    if (!PDPT(pml4)[dirtbl].present) {
        paddr_t paddr = frame_get();
        pd_map(paddr, pml4, dirtbl, flags);
        pages[paddr/PAGE_SIZE].refs++;
    }

    /* Check if table is present */
    if (!PAGE_DIR(pml4, dirtbl)[dir].present) {
        paddr_t paddr = frame_get();
        table_map(paddr, pml4, dirtbl, dir, flags);
        pages[paddr/PAGE_SIZE].refs++;
    }

    PAGE_TBL(pml4, dirtbl, dir)[table] = page;

    return 0;
}

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
    printk("arch_page_unmap(vaddr=%p)\n", vaddr);
    for (;;);
    return 0;
}

static inline __page_t *__page_get_mapping(uintptr_t addr)
{
    __virtaddr_t virt = (__virtaddr_t){.raw = addr};

    if (PML4[virt.pml4].present &&
        PDPT(virt.pml4)[virt.dirtbl].present &&
        PAGE_DIR(virt.pml4, virt.dirtbl)[virt.directory].present &&
        PAGE_TBL(virt.pml4, virt.dirtbl, virt.directory)[virt.table].present) {

        __page_t *page = &PAGE_TBL(virt.pml4, virt.dirtbl, virt.directory)[virt.table];

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
    printk("arch_mm_page_fault(vaddr=%p)\n", vaddr);
    for (;;);
}

paddr_t arch_get_frame()
{
    printk("arch_get_frame()\n");
    for (;;);
    return 0;
}

void arch_release_frame(paddr_t paddr)
{
    printk("arch_release_frame(paddr=%p)\n", paddr);
    for (;;);
}

void arch_switch_mapping(paddr_t map)
{
    printk("arch_switch_mapping(map=%p)\n", map);
    for (;;);
}

void arch_mm_fork(paddr_t base, paddr_t fork)
{
    printk("arch_mm_fork(base=%p, fork=%p)\n", base, fork);
    for (;;);
}

void setup_64_bit_paging()
{
    printk("x86: Setting up 64-bit paging\n");

    uintptr_t __cur_pml4 = read_cr3() & ~PAGE_MASK;
    bootstrap_processor_pml4 = (__dirtbl_t *)    VMA(__cur_pml4);
    bootstrap_processor_pdpt = (__directory_t *) VMA((uintptr_t) GET_PHYS_ADDR(&bootstrap_processor_pml4[0]));
    bootstrap_processor_dir  = (__table_t *)     VMA((uintptr_t) GET_PHYS_ADDR(&bootstrap_processor_pdpt[0]));

    /* Recursive map paging structure */
    bootstrap_processor_pml4[511].raw     = LMA((uintptr_t) bootstrap_processor_pml4);
    bootstrap_processor_pml4[511].write   = 1;
    bootstrap_processor_pml4[511].present = 1;

    /* Map last page table */
    bootstrap_processor_dir[511].raw     = LMA((uintptr_t) last_page_table);
    bootstrap_processor_dir[511].write   = 1;
    bootstrap_processor_dir[511].present = 1;

    /* Unmap lower half */
    bootstrap_processor_pml4[0].raw = 0;

    tlb_flush();
}
