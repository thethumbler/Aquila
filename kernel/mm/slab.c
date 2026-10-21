/*
 *                  Slabs: the kernel's small objects
 *
 * kmalloc() takes objects of up to SLAB_MAX bytes from here, the rest
 * from kvmem. A slab is a page of objects of one size (a power of two from
 * 16 bytes) and one malloc type, its header at the start of the page;
 * each type has a list of its slabs that have free objects, per size, and
 * a slab's free objects are a list too: allocating and freeing take the
 * same time however many objects there are.
 *
 * The pages are mapped in their own region of the kernel's address space
 * (ARCH_SLAB_BASE), which tells kfree() that an object is a slab's; a
 * bitmap has which of its pages are in use. An empty slab's page goes back
 * to the buddy allocator.
 */

#include <core/system.h>
#include <core/panic.h>
#include <core/string.h>

#include <ds/queue.h>
#include <mm/buddy.h>
#include <mm/mm.h>
#include <mm/pmap.h>
#include <mm/vm.h>

struct slab {
    struct slab *next, *prev;   /* the type's slabs of this size with free objects */
    struct malloc_type *type;
    void *free;                 /* its free objects */
    uint32_t size;              /* of its objects */
    uint32_t used;
};

/* the objects start after the header, aligned as kmalloc's are */
#define SLAB_HEADER     ((sizeof(struct slab) + 15) & ~(size_t) 15)
#define SLAB_PAGES      (ARCH_SLAB_SIZE / PAGE_SIZE)

static uint8_t slab_map[SLAB_PAGES / 8];
static size_t slab_first_free;

size_t slab_used;   /* bytes of pages, for /proc/meminfo */

static int slab_class(size_t size)
{
    int class = 0;

    while (((size_t) SLAB_MIN << class) < size)
        ++class;

    return class;
}

/* a page of the region, mapped to a page of memory */
static struct slab *slab_page_alloc(void)
{
    size_t i = slab_first_free;

    while (i < SLAB_PAGES && (slab_map[i / 8] & (1 << (i % 8))))
        ++i;

    if (i >= SLAB_PAGES)
        panic("slab: the region is full");

    slab_map[i / 8] |= 1 << (i % 8);
    slab_first_free = i + 1;

    vaddr_t vaddr = ARCH_SLAB_BASE + i * PAGE_SIZE;
    struct vm_page *vm_page = mm_page_alloc();

    pmap_add(kvm_space.pmap, vaddr, vm_page->paddr, VM_KRW);
    slab_used += PAGE_SIZE;

    return (struct slab *) vaddr;
}

static void slab_page_free(struct slab *slab)
{
    vaddr_t vaddr = (vaddr_t) slab;
    size_t i = (vaddr - ARCH_SLAB_BASE) / PAGE_SIZE;
    paddr_t paddr = arch_page_get_mapping(kvm_space.pmap, vaddr);

    pmap_remove(kvm_space.pmap, vaddr, vaddr + PAGE_SIZE);
    buddy_free(BUDDY_ZONE_NORMAL, paddr, PAGE_SIZE);
    slab_used -= PAGE_SIZE;

    slab_map[i / 8] &= ~(1 << (i % 8));

    if (i < slab_first_free)
        slab_first_free = i;
}

static void slab_unlink(struct slab **list, struct slab *slab)
{
    if (slab->prev)
        slab->prev->next = slab->next;
    else
        *list = slab->next;

    if (slab->next)
        slab->next->prev = slab->prev;

    slab->next = slab->prev = NULL;
}

static void slab_link(struct slab **list, struct slab *slab)
{
    slab->prev = NULL;
    slab->next = *list;

    if (*list)
        (*list)->prev = slab;

    *list = slab;
}

void *slab_alloc(size_t size, struct malloc_type *type)
{
    int class = slab_class(size);
    struct slab **list = &type->slabs[class];
    struct slab *slab = *list;

    if (!slab) {
        slab = slab_page_alloc();
        slab->type = type;
        slab->size = SLAB_MIN << class;
        slab->used = 0;
        slab->free = NULL;

        /* every object free, the first at the head of the list */
        for (size_t off = PAGE_SIZE - slab->size; off >= SLAB_HEADER; off -= slab->size) {
            void **obj = (void **) ((char *) slab + off);
            *obj = slab->free;
            slab->free = obj;
        }

        slab_link(list, slab);
    }

    void **obj = slab->free;
    slab->free = *obj;
    slab->used++;

    /* full: no more on the list */
    if (!slab->free)
        slab_unlink(list, slab);

    type->nr++;
    type->total += slab->size;

    return obj;
}

int slab_owns(void *ptr)
{
    uintptr_t addr = (uintptr_t) ptr;
    return addr >= ARCH_SLAB_BASE && addr < ARCH_SLAB_BASE + ARCH_SLAB_SIZE;
}

void slab_free(void *ptr)
{
    struct slab *slab = (struct slab *) PAGE_ALIGN((uintptr_t) ptr);
    struct malloc_type *type = slab->type;
    struct slab **list = &type->slabs[slab_class(slab->size)];

    int was_full = !slab->free;

    *(void **) ptr = slab->free;
    slab->free = ptr;
    slab->used--;

    type->nr--;
    type->total -= slab->size;

    if (!slab->used) {
        if (!was_full)
            slab_unlink(list, slab);

        slab_page_free(slab);
    } else if (was_full) {
        slab_link(list, slab);
    }
}
