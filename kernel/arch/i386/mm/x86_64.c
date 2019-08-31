#include <core/arch.h>
#include <core/panic.h>
#include <core/printk.h>
#include <core/system.h>
#include <cpu/cpu.h>
#include <ds/queue.h>
#include <mm/buddy.h>
#include <mm/vm.h>

#include "x86_64.h"

MALLOC_DEFINE(M_PMAP, "pmap", "physical memory map structure");

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
    asm volatile ("invlpg (%%rax)"::"a"(virt));
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

    //pages[GET_PHYS_ADDR(&LVL4[pml4])/PAGE_SIZE].refs++;
    mm_page_incref(GET_PHYS_ADDR(&LVL4[pml4]));

    /* Check if PD is present */
    if (!LVL3(pml4)[dirtbl].present) {
        paddr_t paddr = frame_get();
        pd_map(paddr, pml4, dirtbl, flags);
    }

    //pages[GET_PHYS_ADDR(&LVL3(pml4)[dirtbl])/PAGE_SIZE].refs++;
    mm_page_incref(GET_PHYS_ADDR(&LVL3(pml4)[dirtbl]));

    /* Check if table is present */
    if (!LVL2(pml4, dirtbl)[dir].present) {
        paddr_t paddr = frame_get();
        table_map(paddr, pml4, dirtbl, dir, flags);
    }

    //pages[GET_PHYS_ADDR(&LVL2(pml4, dirtbl)[dir])/PAGE_SIZE].refs++;
    mm_page_incref(GET_PHYS_ADDR(&LVL2(pml4, dirtbl)[dir]));

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

paddr_t arch_page_get_mapping(struct pmap *pmap, vaddr_t vaddr)
{
    __page_t *page = __page_get_mapping(vaddr);

    if (page)
        return GET_PHYS_ADDR(page);

    return 0;
}

void arch_mm_page_fault(vaddr_t vaddr, int err)
{
    //printk("arch_mm_page_fault(vaddr=%p)\n", vaddr);
    int flags = 0;

    flags |= err & 0x01? PF_PRESENT : 0;
    flags |= err & 0x02? PF_WRITE   : PF_READ;
    flags |= err & 0x04? PF_USER    : 0;
    flags |= err & 0x10? PF_EXEC    : 0;

    mm_page_fault(vaddr, flags);
}

paddr_t arch_get_frame()
{
    return frame_get();
}

void arch_release_frame(paddr_t paddr)
{
    frame_release(paddr);
}

static struct pmap *cur_pmap = NULL;
struct pmap *arch_pmap_switch(struct pmap *pmap)
{
    if (!pmap)
        panic("pmap?");

    struct pmap *ret = cur_pmap;

    if (cur_pmap && cur_pmap->map == pmap->map) {
        cur_pmap == pmap;
        return ret;
    }

    if (cur_pmap) { /* Store current directory mapping in old_dir */
        copy_virtual_to_physical((void *) cur_pmap->map, (void *) bsp_lvl4, 255 * 8);
    }

    copy_physical_to_virtual((void *) bsp_lvl4, (void *) pmap->map, 255 * 8);

    cur_pmap = pmap;
    tlb_flush();

    return ret;
}

static struct pmap k_pmap;

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

static struct pmap *pmap_alloc(void)
{
    return kmalloc(sizeof(struct pmap), &M_PMAP, 0);
}

static void pmap_release(struct pmap *pmap)
{
    if (pmap == cur_pmap) {
        arch_pmap_switch(&k_pmap);
        //cur_pmap = NULL;
    }

    if (pmap->map)
        arch_release_frame(pmap->map);

    kfree(pmap);
}

void arch_pmap_init(void)
{
    setup_64_bit_paging();
    cur_pmap = &k_pmap;
}

struct pmap *arch_pmap_create(void)
{
    struct pmap *pmap = pmap_alloc();

    if (pmap == NULL)
        return NULL;

    memset(pmap, 0, sizeof(struct pmap));

    pmap->map = arch_get_frame();
    pmap->ref = 1;

    return pmap;
}

#if 0
void arch_pmap_incref(struct pmap *pmap)
{
    /* XXX Handle overflow */
    pmap->ref++;
}
#endif

void arch_pmap_decref(struct pmap *pmap)
{
    pmap->ref--;

    if (pmap->ref == 0)
        pmap_release(pmap);
}

int arch_pmap_add(struct pmap *pmap, vaddr_t va, paddr_t pa, uint32_t flags)
{
    //printk("arch_pmap_add(pmap=%p, va=%p, pa=%p, flags=0x%x)\n", pmap, va, pa, flags);

    struct pmap *old_map = arch_pmap_switch(pmap);

    if (va & PAGE_MASK) {
        struct pmap *old_map = arch_pmap_switch(pmap);
        return -EINVAL;
    }

    __virtaddr_t virtaddr = {.raw = va};

    size_t pml4   = virtaddr.pml4;
    size_t dirtbl = virtaddr.dirtbl;
    size_t dir    = virtaddr.directory;
    size_t table  = virtaddr.table;

    __page_map(pa, pml4, dirtbl, dir, table, flags);

    tlb_invalidate_page(va);

    arch_pmap_switch(old_map);

    return 0;
}

void arch_pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva)
{
    struct pmap *old_map = arch_pmap_switch(pmap);

    if (sva & PAGE_MASK || eva & PAGE_MASK) {
        arch_pmap_switch(old_map);
        return;
    }

    while (sva < eva) {
        page_unmap(sva);
        sva += PAGE_SIZE;
    }

    arch_pmap_switch(old_map);
}

void arch_pmap_remove_all(struct pmap *pmap)
{
    struct pmap *old_map = arch_pmap_switch(pmap);

    paddr_t base = pmap->map;

    uintptr_t old_mount = frame_mount(base);
    uint32_t *tbl = (uint32_t *) MOUNT_ADDR;

    for (int i = 0; i < 768; ++i) {
        if (tbl[i] & PG_PRESENT) {
            paddr_t table = GET_PHYS_ADDR(tbl[i]);

            uintptr_t _mnt = frame_mount(table);
            uint32_t  *pg  = MOUNT_ADDR;

            for (int j = 0; j < 1024; ++j) {
                if (pg[j] & PG_PRESENT) {
                    paddr_t page = GET_PHYS_ADDR(pg[j]);
                    pg[j] = 0;
                    mm_page_decref(page);
                    mm_page_decref(table);
                }
            }

            tbl[i] = 0;

            frame_mount(_mnt);
        }
    }

    tlb_flush();

    frame_mount(old_mount);

    arch_pmap_switch(old_map);
    return;
}

void arch_pmap_protect(struct pmap *pmap, vaddr_t sva, vaddr_t eva, uint32_t prot)
{
    struct pmap *old_map = arch_pmap_switch(pmap);

    //printk("arch_pmap_protect(pmap=%p, sva=%p, eva=%p, prot=0x%x)\n", pmap, sva, eva, prot);

    if (sva & PAGE_MASK) {
        arch_pmap_switch(old_map);
        return;
    }

    while (sva < eva) {
        size_t pdidx = VDIR(sva);
        size_t ptidx = VTBL(sva);

        page_protect(pdidx, ptidx, prot);
        tlb_invalidate_page(sva);

        sva += PAGE_SIZE;
    }

    arch_pmap_switch(old_map);
}

void arch_pmap_copy(struct pmap *dst_map, struct pmap *src_map, vaddr_t dst_addr, size_t len,
 vaddr_t src_addr)
{
    return;
}

void arch_pmap_update(struct pmap *pmap)
{
    return;
}

static char __copy_buf[PAGE_SIZE] __aligned(PAGE_SIZE);
void arch_pmap_page_copy(paddr_t src, paddr_t dst)
{
    copy_physical_to_virtual(__copy_buf, (char *) src, PAGE_SIZE);
    copy_virtual_to_physical((char *) dst, __copy_buf, PAGE_SIZE);
    return;
}

void arch_pmap_page_protect(struct vm_page *pg, uint32_t flags)
{
    return;
}

int arch_pmap_clear_modify(struct vm_page *pg)
{
    return -1;
}

int arch_pmap_clear_reference(struct vm_page *pg)
{
    return -1;
}

int arch_pmap_is_modified(struct vm_page *pg)
{
    return -1;
}

int arch_pmap_is_referenced(struct vm_page *pg)
{
    return -1;
}

paddr_t arch_page_get_mapping(struct pmap *pmap, vaddr_t vaddr)
{
    struct pmap *old_map = arch_pmap_switch(pmap);

    //printk("arch_page_get_mapping(vaddr=%p)\n", vaddr);
    uint32_t page = __page_get_mapping(vaddr);

    if (page) {
        arch_pmap_switch(old_map);

        return GET_PHYS_ADDR(page);
    }

    arch_pmap_switch(old_map);

    return 0;
}
