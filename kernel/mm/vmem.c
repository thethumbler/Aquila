/*
 * vmem: an allocator of ranges (mm/vmem.h)
 */

#include <core/system.h>
#include <core/panic.h>
#include <core/string.h>
#include <mm/vmem.h>

MALLOC_DEFINE(M_VMEM_SEG, "vmem-seg", "vmem segment");

/* floor and ceiling of log2(n), n > 0 */
static int log2_floor(size_t n)
{
    return (int) (sizeof(size_t) * 8 - 1) - __builtin_clzl(n);
}

static int log2_ceil(size_t n)
{
    int l = log2_floor(n);
    return ((size_t) 1 << l) == n ? l : l + 1;
}

static struct vmem_seg **free_list(struct vmem *vm, struct vmem_seg *seg)
{
    return &vm->free[log2_floor(seg->size / vm->quantum)];
}

static struct vmem_seg **hash_chain(struct vmem *vm, uintptr_t base)
{
    return &vm->hash[(base / vm->quantum) % VMEM_HASH];
}

/* on and off a list (free list, or hash chain): lprev, lnext */
static void list_insert(struct vmem_seg **list, struct vmem_seg *seg)
{
    seg->lprev = NULL;
    seg->lnext = *list;

    if (*list)
        (*list)->lprev = seg;

    *list = seg;
}

static void list_remove(struct vmem_seg **list, struct vmem_seg *seg)
{
    if (seg->lprev)
        seg->lprev->lnext = seg->lnext;
    else
        *list = seg->lnext;

    if (seg->lnext)
        seg->lnext->lprev = seg->lprev;

    seg->lprev = seg->lnext = NULL;
}

/* in the arena's address order: `seg' after `prev' (NULL: first) */
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

static void seg_unlink(struct vmem *vm, struct vmem_seg *seg)
{
    if (seg->prev)
        seg->prev->next = seg->next;
    else
        vm->segs = seg->next;

    if (seg->next)
        seg->next->prev = seg->prev;
}

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

struct vmem_seg *vmem_alloc(struct vmem *vm, size_t size)
{
    size_t n = (size + vm->quantum - 1) / vm->quantum;
    struct vmem_seg *seg = NULL;

    if (!n)
        n = 1;

    /* instant fit: every segment on list ceil(log2 n) and above is big
     * enough */
    for (size_t i = log2_ceil(n); i < VMEM_LISTS && !seg; ++i)
        seg = vm->free[i];

    /* else one on list floor(log2 n) may be */
    if (!seg) {
        for (struct vmem_seg *s = vm->free[log2_floor(n)]; s && !seg; s = s->lnext)
            if (s->size >= n * vm->quantum)
                seg = s;
    }

    if (!seg)
        return NULL;

    size = n * vm->quantum;

    /* the rest of it stays free */
    if (seg->size > size) {
        struct vmem_seg *rest = kmalloc(sizeof(*rest), &M_VMEM_SEG, M_ZERO);

        if (!rest)
            return NULL;

        list_remove(free_list(vm, seg), seg);

        rest->base = seg->base + size;
        rest->size = seg->size - size;
        rest->free = 1;
        seg->size  = size;

        seg_link(vm, seg, rest);
        list_insert(free_list(vm, rest), rest);
    } else {
        list_remove(free_list(vm, seg), seg);
    }

    seg->free = 0;
    list_insert(hash_chain(vm, seg->base), seg);
    vm->inuse += seg->size;

    return seg;
}

struct vmem_seg *vmem_find(struct vmem *vm, uintptr_t base)
{
    for (struct vmem_seg *seg = *hash_chain(vm, base); seg; seg = seg->lnext)
        if (seg->base == base)
            return seg;

    return NULL;
}

/* `b' (right after `a', both free) becomes part of `a' */
static void seg_merge(struct vmem *vm, struct vmem_seg *a, struct vmem_seg *b)
{
    list_remove(free_list(vm, a), a);
    list_remove(free_list(vm, b), b);

    a->size += b->size;
    seg_unlink(vm, b);
    kfree(b);

    list_insert(free_list(vm, a), a);
}

void vmem_free(struct vmem *vm, struct vmem_seg *seg)
{
    if (seg->free)
        panic("vmem_free: a free segment");

    list_remove(hash_chain(vm, seg->base), seg);
    vm->inuse -= seg->size;

    seg->free  = 1;
    seg->type  = NULL;
    seg->bytes = 0;
    list_insert(free_list(vm, seg), seg);

    /* the free neighbours, one segment with it */
    if (seg->next && seg->next->free)
        seg_merge(vm, seg, seg->next);

    if (seg->prev && seg->prev->free)
        seg_merge(vm, seg->prev, seg);
}
