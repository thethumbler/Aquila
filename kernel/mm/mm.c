/**
 * \defgroup mm kernel/mm
 * \brief Memory Management
 *
 * The memory management subsystem is responsible for the allocation
 * of physical memory pages, management of virtual memory of processes
 * and caching of virtual memory objects.
 */

#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>
#include <core/string.h>

#include <boot/boot.h>

#include <mm/pmap.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/buddy.h>

#include <sys/sched.h>

/* a page structure for every page of RAM, up to its end (mm_setup) */
static struct vm_page *pages;
static size_t pages_nr;
#define PAGE(addr)    (pages[(addr)/PAGE_SIZE])

/* pages[] tracks memory, not what is past it (device memory, such as a
 * framebuffer mapped by a process): no reference counts for those */
#define TRACKED(addr) ((addr)/PAGE_SIZE < pages_nr)

/** 
 * \ingroup mm
 * \brief increment references count of a physical page
 */
void mm_page_incref(paddr_t paddr)
{
    if (TRACKED(paddr))
        PAGE(paddr).ref++;
}

/**
 * \ingroup mm
 * \brief decrement references count of a physical page
 */
void mm_page_decref(paddr_t paddr)
{
    if (TRACKED(paddr))
        PAGE(paddr).ref--;
}

/**
 * \ingroup mm
 * \brief get reference counts of a physical page
 */
size_t mm_page_ref(paddr_t paddr)
{
    return TRACKED(paddr) ? PAGE(paddr).ref : 0;
}

/**
 * \ingroup mm
 * \brief get the virtual memory structure associated with a physical page
 */
struct vm_page *mm_page(paddr_t paddr)
{
    return &PAGE(paddr);
}

/**
 * \ingroup mm
 * \brief allocate an unused page from physical memory
 */
struct vm_page *mm_page_alloc(void)
{
    /* Get new frame */
    paddr_t paddr = buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);
    struct vm_page *vm_page = &PAGE(paddr);

    memset(vm_page, 0, sizeof(struct vm_page));
    vm_page->paddr = paddr;

    return vm_page;
}

/**
 * \ingroup mm
 * \brief deallocate a page
 */
void mm_page_dealloc(paddr_t paddr)
{
    /* TODO: Check out of bounds */
    buddy_free(BUDDY_ZONE_NORMAL, paddr, PAGE_SIZE);

    /* Release frame if it is no longer referenced */
    //if (mm_page_ref(paddr) == 0)
    //    buddy_free(BUDDY_ZONE_NORMAL, paddr, PAGE_SIZE);
}

int mm_page_map(struct pmap *pmap, vaddr_t vaddr, paddr_t paddr, int flags)
{
    /* TODO: Check out of bounds */

    /* Increment references count to physical page */
    //mm_page_incref(paddr);

    return pmap_add(pmap, vaddr, paddr, flags);
}

int mm_page_unmap(struct pmap *pmap, vaddr_t vaddr)
{
    //printk("mm_page_unmap(pmap=%p, vaddr=%p)\n", pmap, vaddr);

    /* TODO: Check out of bounds */

    /* Check if page is mapped */
    paddr_t paddr = arch_page_get_mapping(pmap, vaddr);

    if (paddr) {
        /* Decrement references count to physical page */
        //mm_page_decref(paddr);

        /* Call arch specific page unmapper */
        pmap_remove(pmap, vaddr, vaddr + PAGE_SIZE);

        /* Release page -- checks ref count */
        //mm_page_dealloc(paddr);

        return 0;
    }

    return -EINVAL;
}

int mm_map(struct pmap *pmap, paddr_t paddr, vaddr_t vaddr, size_t size, int flags)
{
    //printk("mm_map(pmap=%p, paddr=%p, vaddr=%p, size=%d, flags=%x)\n", pmap, paddr, vaddr, size, flags);

    /* TODO: Check out of bounds */

    int alloc = !paddr;

    vaddr_t endaddr = PAGE_ROUND(vaddr + size);
    vaddr = PAGE_ALIGN(vaddr);
    paddr = PAGE_ALIGN(paddr);

    size_t nr = (endaddr - vaddr) / PAGE_SIZE;

    while (nr--) {
        paddr_t phys = arch_page_get_mapping(pmap, vaddr);

        if (!phys) {
            if (alloc) {
                paddr = mm_page_alloc()->paddr;
                //printk("paddr = %p\n", paddr);
            }
            mm_page_map(pmap, vaddr, paddr, flags);
        }

        vaddr += PAGE_SIZE;
        paddr += PAGE_SIZE;
    }

    return 0;
}

void mm_unmap(struct pmap *pmap, vaddr_t vaddr, size_t size)
{
    //printk("mm_unmap(pmap=%p, vaddr=%p, size=%ld)\n", pmap, vaddr, size);
    /* TODO: Check out of bounds */

    if (size < PAGE_SIZE) return;

    vaddr_t sva = PAGE_ROUND(vaddr);
    vaddr_t eva = PAGE_ALIGN(vaddr + size);

    //pmap_remove(pmap, sva, eva);

#if 1
    size_t nr = (eva - sva)/PAGE_SIZE;

    while (nr--) {
        mm_page_unmap(pmap, sva);
        sva += PAGE_SIZE;
    }
#endif
}

void mm_unmap_full(struct pmap *pmap, vaddr_t vaddr, size_t size)
{
    //printk("mm_unmap_full(pmap=%p, vaddr=%p, size=%ld)\n", pmap, vaddr, size);

    /* TODO: Check out of bounds */

    vaddr_t start = PAGE_ALIGN(vaddr);
    vaddr_t end   = PAGE_ROUND(vaddr + size);

    size_t nr = (end - start)/PAGE_SIZE;

    while (nr--) {
        mm_page_unmap(pmap, start);
        start += PAGE_SIZE;
    }
}

/*
 * Memory for what the size of the RAM decides (pages[], the buddy
 * allocator's bitmaps): after the kernel, where nothing the boot loader
 * left is, taken before the buddy allocator knows what is free and then
 * marked used. It is reached through the boot mapping (VMA()).
 */
static paddr_t boot_mem_start, boot_mem_next, boot_mem_end;

void *mm_boot_alloc(size_t size, size_t align)
{
    paddr_t paddr = (boot_mem_next + align - 1) & ~(paddr_t) (align - 1);

    if (paddr + size > boot_mem_end)
        panic("mm: out of boot memory");

    boot_mem_next = paddr + size;

    void *ptr = (void *) VMA((uintptr_t) paddr);
    memset(ptr, 0, size);
    return ptr;
}

/* the end of what [start, start + size) overlaps that the boot loader
 * left in memory (modules, the kernel's symbols, the command line), 0 if
 * nothing */
static paddr_t boot_left(struct boot *boot, paddr_t start, size_t size)
{
    paddr_t end = start + size;

    for (int i = 0; i < boot->modules_count; ++i) {
        paddr_t s = LMA((uintptr_t) boot->modules[i].addr);
        paddr_t e = s + boot->modules[i].size;

        if (start < e && end > s)
            return e;
    }

    /* the kernel's symbols and string table, the command line */
    paddr_t r[3][2] = {{0, 0}, {0, 0}, {0, 0}};

    if (boot->symtab) {
        r[0][0] = LMA((uintptr_t) boot->symtab->sh_addr);
        r[0][1] = r[0][0] + boot->symtab->sh_size;
    }

    if (boot->strtab) {
        r[1][0] = LMA((uintptr_t) boot->strtab->sh_addr);
        r[1][1] = r[1][0] + boot->strtab->sh_size;
    }

    if (boot->cmdline) {
        r[2][0] = LMA((uintptr_t) boot->cmdline);
        r[2][1] = r[2][0] + strlen(boot->cmdline) + 1;
    }

    for (int i = 0; i < 3; ++i)
        if (r[i][1] && start < r[i][1] && end > r[i][0])
            return r[i][1];

    return 0;
}

static int boot_usable(struct boot *boot, paddr_t start, size_t size)
{
    for (int i = 0; i < boot->mmap_count; ++i)
        if (boot->mmap[i].type == MMAP_USABLE &&
            boot->mmap[i].start <= start && start + size <= boot->mmap[i].end)
            return 1;

    return 0;
}

/* where the boot memory goes: the first place after the kernel that is
 * RAM, free, and in the boot mapping */
static void boot_mem_place(struct boot *boot, size_t size)
{
    extern char kernel_end;
    paddr_t start = PAGE_ROUND((uintptr_t) &kernel_end), end;

    while ((end = boot_left(boot, start, size)))
        start = PAGE_ROUND(end);

    if (start + size > ARCH_BOOT_MAPPED || !boot_usable(boot, start, size))
        panic("mm: no room for the page structures after the kernel");

    boot_mem_start = boot_mem_next = start;
    boot_mem_end = start + size;
}

/* RAM is [0, the end of the last usable region of the memory map), what
 * isn't usable in it (the regions the map reserves, and its gaps) is
 * marked used */
static void mm_mark_unusable(struct boot *boot, paddr_t ram_end)
{
    paddr_t at = 0;

    while (at < ram_end) {
        /* the usable region that starts the lowest at or after `at' */
        paddr_t next = ram_end, next_end = ram_end;

        for (int i = 0; i < boot->mmap_count; ++i) {
            mmap_t *m = &boot->mmap[i];

            if (m->type != MMAP_USABLE || m->end <= at)
                continue;

            paddr_t s = m->start < at ? at : m->start;

            if (s < next) {
                next = s;
                next_end = m->end;
            }
        }

        if (next > at)
            buddy_set_unusable(at, next - at);

        at = next_end;
    }

    for (int i = 0; i < boot->mmap_count; ++i)
        if (boot->mmap[i].type != MMAP_USABLE && boot->mmap[i].start < ram_end)
            buddy_set_unusable(boot->mmap[i].start,
                MIN(boot->mmap[i].end, ram_end) - boot->mmap[i].start);
}

void mm_setup(struct boot *boot)
{
    /* the end of RAM, and how much there is */
    paddr_t ram_end = 0;
    uint64_t ram = 0;

    for (int i = 0; i < boot->mmap_count; ++i) {
        if (boot->mmap[i].type == MMAP_USABLE) {
            ram += boot->mmap[i].end - boot->mmap[i].start;
            ram_end = MAX(ram_end, boot->mmap[i].end);
        }
    }

    /* no memory map: what the boot loader says there is from 0 */
    if (!ram_end) {
        ram_end = (paddr_t) boot->total_mem * 1024;
        ram = ram_end;
    }

    /* the buddy allocator works in blocks of BUDDY_MAX_BS */
    ram_end &= ~(paddr_t) (BUDDY_MAX_BS - 1);

    printk("kernel: %ld MiB of memory, up to %p\n", ram >> 20, ram_end);

    pages_nr = ram_end / PAGE_SIZE;

    /* the page structures, and the buddy allocator's bitmaps (two bits a
     * page at most, over all its orders) */
    size_t pages_size = pages_nr * sizeof(struct vm_page);
    boot_mem_place(boot, PAGE_ROUND(pages_size + pages_nr / 4 + 0x10000));

    pages = mm_boot_alloc(pages_size, sizeof(uintptr_t));

    buddy_setup(ram_end);

    buddy_set_unusable(boot_mem_start, PAGE_ROUND(boot_mem_next) - boot_mem_start);
    mm_mark_unusable(boot, ram_end);

    /* Mark modules space as unusable */
    for (int i = 0; i < boot->modules_count; ++i) {
        module_t *mod = &boot->modules[i];
        buddy_set_unusable(LMA((uintptr_t) mod->addr), mod->size);
    }

    /* what /proc/meminfo calls the total */
    extern size_t k_total_mem;
    k_total_mem = ram;

    arch_mm_setup();
}
