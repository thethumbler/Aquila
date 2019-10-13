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

#include <boot/boot.h>

#include <mm/pmap.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/buddy.h>

#include <sys/sched.h>

/* FIXME use boot time allocation scheme */
struct vm_page pages[768*1024];
#define PAGE(addr)    (pages[(addr)/PAGE_SIZE])

/** 
 * \ingroup mm
 * \brief increment references count of a physical page
 */
void mm_page_incref(paddr_t paddr)
{
    PAGE(paddr).ref++;
}

/**
 * \ingroup mm
 * \brief decrement references count of a physical page
 */
void mm_page_decref(paddr_t paddr)
{
    PAGE(paddr).ref--;
}

/**
 * \ingroup mm
 * \brief get reference counts of a physical page
 */
size_t mm_page_ref(paddr_t paddr)
{
    return PAGE(paddr).ref;
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

void mm_setup(struct boot *boot)
{
    printk("kernel: total memory: %d KiB, %d MiB\n", boot->total_mem, boot->total_mem / 1024);

    buddy_setup(boot->total_mem * 1024);

    /* Setup memory regions */
    for (int i = 0; i < boot->mmap_count; ++i) {
        if (boot->mmap[i].type == MMAP_RESERVED) {
            size_t size = boot->mmap[i].end - boot->mmap[i].start;
            buddy_set_unusable(boot->mmap[i].start, size);
        }
    }

    /* Mark modules space as unusable */
    for (int i = 0; i < boot->modules_count; ++i) {
        module_t *mod = &boot->modules[i];
        buddy_set_unusable(LMA((uintptr_t) mod->addr), mod->size);
    }

    arch_mm_setup();
}
