/*
 *                 Intel 32-Bit Paging Mode Handler
 *
 *  References:
 *      [1] - Intel(R) 64 and IA-32 Architectures Software Developers Manual
 *          - Volume 3, System Programming Guide
 *          - Chapter 4, Paging
 *
 */

#include <core/arch.h>
#include <core/panic.h>
#include <core/printk.h>
#include <core/system.h>
#include <cpu/cpu.h>
#include <ds/queue.h>

#include <mm/pmap.h>
#include <mm/buddy.h>
#include <mm/vm.h>

#include <sys/sched.h>

#include "i386.h"

MALLOC_DEFINE(M_PMAP, "pmap", "physical memory map structure");

static struct pmap *cur_pmap = NULL;

static volatile uint32_t *bootstrap_processor_table = NULL;
static volatile uint32_t last_page_table[1024] __aligned(PAGE_SIZE) = {0};

static inline void tlb_invalidate_page(uintptr_t virt)
{
    asm volatile("invlpg (%%eax)"::"a"(virt));
}

#define MOUNT_ADDR  ((void *) 0xFFBFF000)
#define PAGE_DIR    ((uint32_t *) 0xFFFFF000)
#define PAGE_TBL(i) ((uint32_t *) (0xFFC00000 + 0x1000 * (i)))

/* ================== Frame Helpers ================== */

static inline uintptr_t frame_mount(uintptr_t paddr)
{
    uintptr_t prev = (uintptr_t) last_page_table[1023] & ~PAGE_MASK;

    if (!paddr)
        return prev; //(uintptr_t) last_page_table[1023] & ~PAGE_MASK;

    if (paddr & PAGE_MASK)
        panic("Mount must be on page (4K) boundary");

    uint32_t page = paddr | PG_PRESENT | PG_WRITE;

    last_page_table[1023] = page;
    tlb_invalidate_page((uintptr_t) MOUNT_ADDR);

    return prev;
}

static inline uintptr_t frame_get(void)
{
    struct vm_page *vm_page = mm_page_alloc();
    uintptr_t frame = vm_page->paddr; //buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);

    if (!frame) {
        panic("Could not allocate frame");
    }

    uintptr_t old = frame_mount(frame);
    memset(MOUNT_ADDR, 0, PAGE_SIZE);
    frame_mount(old);

    return frame;
}

static inline uintptr_t frame_get_no_clr(void)
{
    struct vm_page *vm_page = mm_page_alloc();
    uintptr_t frame = vm_page->paddr; //buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);

    if (!frame) {
        panic("Could not allocate frame");
    }

    return frame;
}

static void frame_release(uintptr_t i)
{
    buddy_free(BUDDY_ZONE_NORMAL, i, PAGE_SIZE);
}

/* ================== Table Helpers ================== */

static inline paddr_t table_alloc(void)
{
    uintptr_t paddr = frame_get();

    struct vm_page *vm_page = mm_page(paddr);
    memset(vm_page, 0, sizeof(struct vm_page));
    vm_page->paddr = paddr;

    return paddr;
}

static inline void table_dealloc(paddr_t paddr)
{
    frame_release(paddr);
}

static inline int table_map(paddr_t paddr, size_t pdidx, int flags)
{
    if (pdidx > 1023)
        return -EINVAL;

    uint32_t table;
    table  = paddr | (PG_PRESENT|PG_WRITE|PG_USER);
    PAGE_DIR[pdidx] = table;

    tlb_flush();

    return 0;
}

static inline void table_unmap(size_t pdidx)
{
    if (pdidx > 1023)
        return;

    if (PAGE_DIR[pdidx] & PG_PRESENT) {
        PAGE_DIR[pdidx] &= ~PG_PRESENT;
        table_dealloc(PAGE_DIR[pdidx] & ~PAGE_MASK);
    }

    tlb_flush();
}

/* ================== Page Helpers ================== */
static inline int page_map(paddr_t paddr, size_t pdidx, size_t ptidx, int flags)
{
    /* Sanity checking */
    if (pdidx > 1023 || ptidx > 1023) {
        return -EINVAL;
    }

    uint32_t page;

    page  = paddr | PG_PRESENT;
    page |= flags & (VM_KW | VM_UW)? PG_WRITE : 0;
    page |= flags & (VM_URWX)? PG_USER : 0;

    /* Check if table is present */
    if (!(PAGE_DIR[pdidx] & PG_PRESENT)) {
        //struct vm_page *vm_page = mm_page_alloc();
        //paddr_t table = vm_page->paddr; //table_alloc();
        paddr_t table = table_alloc();

        table_map(table, pdidx, flags);
    }

    PAGE_TBL(pdidx)[ptidx] = page;

    /* Increment references to table */
    paddr_t table = PHYSADDR(PAGE_DIR[pdidx]);
    mm_page_incref(table);

    return 0;
}

static inline int page_protect(size_t pdidx, size_t ptidx, uint32_t flags)
{
    /* Sanity checking */
    if (pdidx > 1023 || ptidx > 1023) {
        return -EINVAL;
    }

    uint32_t page;

    /* Check if table is present */
    if (!(PAGE_DIR[pdidx] & PG_PRESENT)) {
        return -EINVAL;
    }

    page = PAGE_TBL(pdidx)[ptidx];

    if (page & PG_PRESENT) {
        page &= ~(PG_WRITE|PG_USER);
        page |= (flags & (VM_KW | VM_UW))? PG_WRITE : 0;
        page |= (flags & VM_URWX)? PG_USER : 0;

        PAGE_TBL(pdidx)[ptidx] = page;
    }

    return 0;
}

static inline void page_unmap(vaddr_t vaddr)
{
    if (vaddr & PAGE_MASK)
        return;

    size_t pdidx = VDIR(vaddr);
    size_t ptidx = VTBL(vaddr);

    if (PAGE_DIR[pdidx] & PG_PRESENT) {
        if (PAGE_TBL(pdidx)[ptidx] & PG_PRESENT) {
            uintptr_t old_page = PAGE_ALIGN(PAGE_TBL(pdidx)[ptidx]);
            //printk("old_page %p (ref=%d)\n", old_page, mm_page_ref(old_page));

            PAGE_TBL(pdidx)[ptidx] = 0;

            /* Decrement references to table */
            paddr_t table = PHYSADDR(PAGE_DIR[pdidx]);
            //printk("mm_page_ref(table) = %d\n", mm_page_ref(table));

            mm_page_decref(table);

            if (mm_page_ref(table) == 0)
                table_unmap(pdidx);

            tlb_invalidate_page(vaddr);
        }
    }
}

static inline uint32_t __page_get_mapping(vaddr_t vaddr)
{
    if (PAGE_DIR[VDIR(vaddr)] & PG_PRESENT) {
        uint32_t page = PAGE_TBL(VDIR(vaddr))[VTBL(vaddr)];

        if (page & PG_PRESENT)
            return page;
    }

    return 0;
}

static void table_dump(paddr_t table)
{
    uintptr_t mnt = frame_mount(table);
    uint32_t *_pages = MOUNT_ADDR;

    printk("Table: %p\n", table);

    for (int i = 0; i < 1024; ++i) {
        if (_pages[i] & PG_PRESENT) {
            paddr_t page = PHYSADDR(_pages[i]);
            size_t ref = mm_page_ref(table);
            printk("  %p: %d\n", page, ref);
        }
    }

    frame_mount(mnt);
}

#include "page_utils.h"

struct pmap *pmap_switch(struct pmap *pmap)
{
    if (!pmap)
        panic("pmap?");

    struct pmap *ret = cur_pmap;

    if (cur_pmap && cur_pmap->map == pmap->map) {
        cur_pmap == pmap;
        return ret;
    }

    if (cur_pmap) { /* Store current directory mapping in old_dir */
        copy_virtual_to_physical((void *) cur_pmap->map, (void *) bootstrap_processor_table, 768 * 4);
    }

    copy_physical_to_virtual((void *) bootstrap_processor_table, (void *) pmap->map, 768 * 4);
    cur_pmap = pmap;
    tlb_flush();

    return ret;
}

static struct pmap k_pmap;

static void setup_i386_paging(void)
{
    printk("x86: setting up 32-bit paging\n");
    uintptr_t __cur_pd = read_cr3() & ~PAGE_MASK;

    bootstrap_processor_table = (uint32_t *) VMA(__cur_pd);
    bootstrap_processor_table[1023] = LMA((uint32_t) bootstrap_processor_table) | PG_WRITE | PG_PRESENT;
    bootstrap_processor_table[1022] = LMA((uint32_t) last_page_table) | PG_PRESENT | PG_WRITE;

    /* Unmap lower half */
    for (int i = 0; bootstrap_processor_table[i]; ++i)
        bootstrap_processor_table[i] = 0;

    tlb_flush();

    k_pmap.map = __cur_pd;
    kvm_space.pmap = &k_pmap;
}

/*
 *  Archeticture Interface
 */

int arch_page_unmap(vaddr_t vaddr)
{
    if (vaddr & PAGE_MASK)
        return -EINVAL;

    page_unmap(vaddr);
    return 0;
}

void arch_mm_page_fault(vaddr_t vaddr, int err)
{
    int flags = 0;

    flags |= err & 0x01? PF_PRESENT : 0;
    flags |= err & 0x02? PF_WRITE   : PF_READ;
    flags |= err & 0x04? PF_USER    : 0;
    flags |= err & 0x10? PF_EXEC    : 0;

    mm_page_fault(vaddr, flags);

    return;
}

static struct pmap *pmap_alloc(void)
{
    return kmalloc(sizeof(struct pmap), &M_PMAP, 0);
}

static void pmap_release(struct pmap *pmap)
{
    if (pmap == cur_pmap) {
        pmap_switch(&k_pmap);
        //cur_pmap = NULL;
    }

    if (pmap->map)
        frame_release(pmap->map);

    kfree(pmap);
}

void pmap_init(void)
{
    setup_i386_paging();
    cur_pmap = &k_pmap;
}

struct pmap *pmap_create(void)
{
    struct pmap *pmap = pmap_alloc();

    if (pmap == NULL)
        return NULL;

    memset(pmap, 0, sizeof(struct pmap));

    pmap->map = frame_get();
    pmap->ref = 1;

    return pmap;
}

void pmap_incref(struct pmap *pmap)
{
    /* XXX Handle overflow */
    pmap->ref++;
}

void pmap_decref(struct pmap *pmap)
{
    pmap->ref--;

    if (pmap->ref == 0) {
        //printk("should release pmap %p\n", pmap);
        pmap_release(pmap);
    }
}

int pmap_add(struct pmap *pmap, vaddr_t va, paddr_t pa, uint32_t flags)
{
    //printk("pmap_add(pmap=%p, va=%p, pa=%p, flags=0x%x)\n", pmap, va, pa, flags);

    if ((va & PAGE_MASK) || (pa & PAGE_MASK))
        return -EINVAL;

    struct pmap *old_map = pmap_switch(pmap);

    size_t pdidx = VDIR(va);
    size_t ptidx = VTBL(va);

    page_map(pa, pdidx, ptidx, flags);
    tlb_invalidate_page(va);

    pmap_switch(old_map);

    return 0;
}

void pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva)
{
    if ((sva & PAGE_MASK) || (eva & PAGE_MASK))
        return;

    struct pmap *old_map = pmap_switch(pmap);

    while (sva < eva) {
        page_unmap(sva);
        sva += PAGE_SIZE;
    }

    pmap_switch(old_map);
}

static void table_remove_all(paddr_t table)
{
    uintptr_t mnt = frame_mount(table);
    uint32_t *_pages = MOUNT_ADDR;

    for (int i = 0; i < 1024; ++i) {
        if (_pages[i] & PG_PRESENT) {
            paddr_t page = PHYSADDR(_pages[i]);
            _pages[i] = 0;

            mm_page_decref(page);
            size_t ref = mm_page_ref(table);
            mm_page_decref(table);
        }
    }

    frame_mount(mnt);
}

void pmap_remove_all(struct pmap *pmap)
{
    //printk("pmap_remove_all(pmap=%p)\n", pmap);

    struct pmap *old_map = pmap_switch(&k_pmap);

    paddr_t base = pmap->map;

    uintptr_t old_mount = frame_mount(base);
    uint32_t *tbl = (uint32_t *) MOUNT_ADDR;

    for (int i = 0; i < 768; ++i) {
        if (tbl[i] & PG_PRESENT) {
            paddr_t table = PHYSADDR(tbl[i]);
            table_remove_all(table);
            tbl[i] = 0;
            table_dealloc(table);
        }
    }

    tlb_flush();

    frame_mount(old_mount);

    pmap_switch(old_map);



    return;
}

void pmap_protect(struct pmap *pmap, vaddr_t sva, vaddr_t eva, uint32_t prot)
{
    if (sva & PAGE_MASK)
        return;

    struct pmap *old_map = pmap_switch(pmap);

    while (sva < eva) {
        size_t pdidx = VDIR(sva);
        size_t ptidx = VTBL(sva);

        page_protect(pdidx, ptidx, prot);
        tlb_invalidate_page(sva);

        sva += PAGE_SIZE;
    }

    pmap_switch(old_map);
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

static char __copy_buf[PAGE_SIZE] __aligned(PAGE_SIZE);
void pmap_page_copy(paddr_t src, paddr_t dst)
{
    copy_physical_to_virtual(__copy_buf, (char *) src, PAGE_SIZE);
    copy_virtual_to_physical((char *) dst, __copy_buf, PAGE_SIZE);
    return;
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
    struct pmap *old_map = pmap_switch(pmap);

    //printk("arch_page_get_mapping(vaddr=%p)\n", vaddr);
    uint32_t page = __page_get_mapping(vaddr);

    if (page) {
        pmap_switch(old_map);

        return PHYSADDR(page);
    }

    pmap_switch(old_map);

    return 0;
}

int pmap_page_read(paddr_t paddr, off_t off, size_t size, void *buf)
{
    size_t sz = MAX(PAGE_SIZE, size - off);
    uintptr_t old = frame_mount(paddr);
    char *page = (char *) MOUNT_ADDR;
    memcpy(buf, page + off, sz);
    frame_mount(old);
    return 0;
}

int pmap_page_write(paddr_t paddr, off_t off, size_t size, void *buf)
{
    size_t sz = MAX(PAGE_SIZE, size - off);
    uintptr_t old = frame_mount(paddr);
    char *page = (char *) MOUNT_ADDR;
    memcpy(page + off, buf, sz);
    frame_mount(old);
    return 0;
}
