/** \file
 * vmem, an allocator of ranges of addresses (see `mm/vmem.h`).
 */

#include <core/system.h>
#include <core/panic.h>
#include <core/string.h>
#include <mm/vmem.h>

MALLOC_DEFINE(M_VMEM_SEG, "vmem-seg", "vmem segment");

/** Returns log2(`n`) rounded down; `n` must not be 0. */
static int log2_floor(size_t n)
{
    return (int) (sizeof(size_t) * 8 - 1) - __builtin_clzl(n);
}

/** Returns log2(`n`) rounded up; `n` must not be 0. */
static int log2_ceil(size_t n)
{
    int l = log2_floor(n);
    return ((size_t) 1 << l) == n ? l : l + 1;
}

/** Returns the free list that a free segment belongs on, by its size. */
static struct vmem_seg **free_list(struct vmem *vm, struct vmem_seg *seg)
{
    return &vm->free[log2_floor(seg->size / vm->quantum)];
}

/** Returns the hash chain of the allocated segments that start at `base`. */
static struct vmem_seg **hash_chain(struct vmem *vm, uintptr_t base)
{
    return &vm->hash[(base / vm->quantum) % VMEM_HASH];
}

/** Puts a segment at the head of a free list or a hash chain, through
 * its `lprev` and `lnext`. */
static void list_insert(struct vmem_seg **list, struct vmem_seg *seg)
{
    seg->lprev = NULL;
    seg->lnext = *list;

    if (*list)
        (*list)->lprev = seg;

    *list = seg;
}

/** Takes a segment off the free list or hash chain it is on. */
static void seg_list_remove(struct vmem_seg **list, struct vmem_seg *seg)
{
    if (seg->lprev)
        seg->lprev->lnext = seg->lnext;
    else
        *list = seg->lnext;

    if (seg->lnext)
        seg->lnext->lprev = seg->lprev;

    seg->lprev = seg->lnext = NULL;
}

/** Links `seg` into the arena's list of segments, in address order, right
 * after `prev` (NULL: as the first). */
static void seg_link(struct vmem *vm, struct vmem_seg *prev, struct vmem_seg *seg)
{
    seg->prev = prev;
    seg->next = prev ? prev->next : vm->segs;

    if (seg->next)
        seg->next->prev = seg;

    if (prev)
        prev->next = seg;
    else
        vm->segs = seg;
}

/** Takes `seg` off the arena's list of segments. */
static void seg_unlink(struct vmem *vm, struct vmem_seg *seg)
{
    if (seg->prev)
        seg->prev->next = seg->next;
    else
        vm->segs = seg->next;

    if (seg->next)
        seg->next->prev = seg->prev;
}

/**
 * Sets up an arena: the range `[base, base + size)`, all free.
 *
 * @param vm       the arena
 * @param name     its name, for messages
 * @param base     the range's start
 * @param size     its size, in bytes (rounded down to a multiple of
 *                 `quantum`)
 * @param quantum  the unit of allocation: every segment's size is a
 *                 multiple of it
 * @return 0, or `-ENOMEM` if there was no memory for the first segment
 */
int vmem_init(struct vmem *vm, const char *name, uintptr_t base, size_t size, size_t quantum)
{
    memset(vm, 0, sizeof(*vm));
    vm->name    = name;
    vm->quantum = quantum;
    vm->size    = size;

    struct vmem_seg *seg = kmalloc(sizeof(*seg), &M_VMEM_SEG, M_ZERO);

    if (!seg)
        return -ENOMEM;

    seg->base = base;
    seg->size = size - size % quantum;
    seg->free = 1;

    seg_link(vm, NULL, seg);
    list_insert(free_list(vm, seg), seg);
    return 0;
}

/**
 * Allocates a range from an arena.
 *
 * @param vm    the arena
 * @param size  the size wanted, in bytes (rounded up to a multiple of the
 *              quantum)
 * @return the allocated segment, or NULL if there is no free range that
 *         large, or no memory for the record of what's left
 */
struct vmem_seg *vmem_alloc(struct vmem *vm, size_t size)
{
    size_t n = (size + vm->quantum - 1) / vm->quantum;
    struct vmem_seg *seg = NULL;

    if (!n)
        n = 1;

    /* instant fit: every segment on list ceil(log2 n) or above is big
     * enough, so the first one found will do */
    for (size_t i = log2_ceil(n); i < VMEM_LISTS && !seg; ++i)
        seg = vm->free[i];

    /* otherwise, a segment on list floor(log2 n) may be big enough */
    if (!seg) {
        for (struct vmem_seg *s = vm->free[log2_floor(n)]; s && !seg; s = s->lnext)
            if (s->size >= n * vm->quantum)
                seg = s;
    }

    if (!seg)
        return NULL;

    size = n * vm->quantum;

    /* the rest of the segment stays free, as a segment of its own */
    if (seg->size > size) {
        struct vmem_seg *rest = kmalloc(sizeof(*rest), &M_VMEM_SEG, M_ZERO);

        if (!rest)
            return NULL;

        seg_list_remove(free_list(vm, seg), seg);

        rest->base = seg->base + size;
        rest->size = seg->size - size;
        rest->free = 1;
        seg->size  = size;

        seg_link(vm, seg, rest);
        list_insert(free_list(vm, rest), rest);
    } else {
        seg_list_remove(free_list(vm, seg), seg);
    }

    seg->free = 0;
    list_insert(hash_chain(vm, seg->base), seg);
    vm->inuse += seg->size;

    return seg;
}

/**
 * Finds the allocated segment that starts at an address.
 *
 * @param vm    the arena
 * @param base  the address
 * @return the segment, or NULL if no allocated segment starts there
 */
struct vmem_seg *vmem_find(struct vmem *vm, uintptr_t base)
{
    for (struct vmem_seg *seg = *hash_chain(vm, base); seg; seg = seg->lnext)
        if (seg->base == base)
            return seg;

    return NULL;
}

/** Merges the free segment `b` into the free segment `a`, which comes
 * right before it. */
static void seg_merge(struct vmem *vm, struct vmem_seg *a, struct vmem_seg *b)
{
    seg_list_remove(free_list(vm, a), a);
    seg_list_remove(free_list(vm, b), b);

    a->size += b->size;
    seg_unlink(vm, b);
    kfree(b);

    list_insert(free_list(vm, a), a);
}

/**
 * Frees an allocated segment. It merges with the free segments next to
 * it, so that the arena doesn't fragment. Freeing a free segment panics.
 *
 * @param vm   the arena
 * @param seg  the segment, which `vmem_alloc()` gave
 */
void vmem_free(struct vmem *vm, struct vmem_seg *seg)
{
    if (seg->free)
        panic("vmem_free: a free segment");

    seg_list_remove(hash_chain(vm, seg->base), seg);
    vm->inuse -= seg->size;

    seg->free  = 1;
    seg->type  = NULL;
    seg->bytes = 0;
    list_insert(free_list(vm, seg), seg);

    /* merge it with its free neighbours */
    if (seg->next && seg->next->free)
        seg_merge(vm, seg, seg->next);

    if (seg->prev && seg->prev->free)
        seg_merge(vm, seg->prev, seg);
}
