/** \file
 * Address spaces: a process's mappings (map entries), kept in address
 * order, and its page tables (`pmap`).
 */

#include <core/system.h>
#include <core/arch.h>

#include <mm/pmap.h>
#include <mm/vm.h>

#include <ds/queue.h>

/**
 * Adds a map entry to an address space, in address order.
 *
 * If the entry's `base` is 0, the entry is placed at the highest free gap
 * between mappings that it fits in. The entry always goes before an
 * existing one (the stack's, at the top of user space): an entry past the
 * last one can't be added.
 *
 * @param vm_space  the address space
 * @param vm_entry  the entry, with its `base` (or 0) and `size` set
 * @return 0, `-EINVAL` if either is NULL, or `-ENOMEM` if there is no
 *         room for it, or no memory for its list node
 */
int vm_space_insert(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    if (!vm_space || !vm_entry)
        return -EINVAL;

    struct queue *queue = &vm_space->vm_entries;

    int alloc = !vm_entry->base;

    uintptr_t end = vm_entry->base + vm_entry->size;

    struct qnode *cur = NULL;
    uintptr_t prev_end = 0;

    if (alloc) {
        /* find the highest gap that the entry fits in */
        queue_for (node, queue) {
            struct vm_entry *cur_vm_entry = (struct vm_entry *) node->value;

            if (cur_vm_entry->base - prev_end >= vm_entry->size) {
                vm_entry->base = cur_vm_entry->base - vm_entry->size;
                end = vm_entry->base + vm_entry->size;
            }

            prev_end = cur_vm_entry->base + cur_vm_entry->size;
        }
    }

    prev_end = 0;

    queue_for (node, queue) {
        struct vm_entry *cur_vm_entry = (struct vm_entry *) node->value;

        if (vm_entry->base && cur_vm_entry->base >= end && prev_end <= vm_entry->base) {
            cur = node;
            break;
        }

        prev_end = cur_vm_entry->base + cur_vm_entry->size;
    }

    if (!cur)
        return -ENOMEM;

    struct qnode *node = kmalloc(sizeof(struct qnode), &M_QNODE, M_ZERO);

    if (!node)
        return -ENOMEM;

    node->value = vm_entry;
    node->next = cur;
    node->prev = cur->prev;

    if (cur->prev)
        cur->prev->next = node;
    else
        queue->head = node;

    cur->prev  = node;
    vm_entry->qnode = node;
    ++queue->count;

    return 0;
}

/**
 * Finds the map entry that an address is in.
 *
 * @param vm_space  the address space
 * @param vaddr     the address
 * @return the entry, or NULL if no mapping covers the address
 */
struct vm_entry *vm_space_find(struct vm_space *vm_space, vaddr_t vaddr)
{
    if (!vm_space) return NULL;

    vaddr = PAGE_ALIGN(vaddr);

    struct queue *vm_entries = &vm_space->vm_entries;

    queue_for (node, vm_entries) {
        struct vm_entry *vm_entry = node->value;
        vaddr_t vm_end = vm_entry->base + vm_entry->size;

        if (vaddr >= vm_entry->base && vaddr < vm_end)
            return vm_entry;
    }

    return NULL;
}

/**
 * Removes all the mappings of an address space: its entries are released
 * and freed, and its page tables emptied.
 *
 * @param vm_space  the address space (NULL does nothing)
 */
void vm_space_destroy(struct vm_space *vm_space)
{
    if (!vm_space)
        return;

    struct vm_entry *vm_entry = NULL;

    while ((vm_entry = dequeue(&vm_space->vm_entries))) {
        vm_entry_destroy(vm_entry);
        kfree(vm_entry);
    }

    pmap_remove_all(vm_space->pmap);
}

/**
 * Copies an address space's mappings into another, for `fork()`. The two
 * share the pages, copy on write: writable private mappings become
 * read-only in `src`, and their anons are marked to be copied, so that
 * the first write to a page gives the writer its own copy
 * (`mm/fault.c`).
 *
 * @param src  the parent's address space
 * @param dst  the child's address space, empty
 * @return 0, `-EINVAL` if either is NULL, or `-ENOMEM` if there was no
 *         memory for an entry; what was copied stays in `dst`, for the
 *         caller to destroy
 */
int vm_space_fork(struct vm_space *src, struct vm_space *dst)
{
    if (!src || !dst)
        return -EINVAL;

    queue_for (node, &src->vm_entries) {
        struct vm_entry *s_entry = node->value;
        struct vm_entry *d_entry = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, 0);

        /* out of memory: what was copied so far goes with dst, when the
         * caller destroys it (fork's error path) */
        if (!d_entry)
            return -ENOMEM;

        memcpy(d_entry, s_entry, sizeof(struct vm_entry));

        if (!(d_entry->qnode = enqueue(&dst->vm_entries, d_entry))) {
            kfree(d_entry);
            return -ENOMEM;
        }

        if (s_entry->vm_anon) {
            s_entry->vm_anon->flags |= VM_COPY;
            vm_anon_incref(s_entry->vm_anon);
        }

        if (s_entry->vm_object) {
            vm_object_incref(s_entry->vm_object);
        }

        if (s_entry->flags & (VM_UW|VM_KW) && !(s_entry->flags & VM_SHARED)) {
            /* make the parent's pages read-only: the first write copies */
            vaddr_t sva = s_entry->base;
            vaddr_t eva = sva + s_entry->size;
            unsigned flags = s_entry->flags & ~(VM_UW|VM_KW);
            pmap_protect(src->pmap, sva, eva, flags);
        }
    }

    return 0;
}
