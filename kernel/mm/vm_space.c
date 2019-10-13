#include <core/system.h>
#include <core/arch.h>

#include <mm/pmap.h>
#include <mm/vm.h>

#include <ds/queue.h>

/**
 * \ingroup mm
 * \brief insert a new vm entry into a vm space
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
        /* look for the last valid entry */
        queue_for (node, queue) {
            struct vm_entry *cur_vm_entry = (struct vm_entry *) node->value;

            if (cur_vm_entry->base - prev_end >= vm_entry->size) {
                vm_entry->base = cur_vm_entry->base - vm_entry->size;
                end = vm_entry->base + vm_entry->size;
            }

            prev_end = cur_vm_entry->base + cur_vm_entry->size;
        }
    }

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
    if (!node) {
        /* TODO */
    }

    node->value = vm_entry;
    node->next = cur;
    node->prev = cur->prev;

    if (cur->prev)
        cur->prev->next = node;

    cur->prev  = node;
    vm_entry->qnode = node;
    ++queue->count;

    return 0;
}

/**
 * \ingroup mm
 * \brief lookup the vm entry containing `vaddr` inside a vm space
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
 * \ingroup mm
 * \brief destroy all resources associated with a vm space
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
 * \ingroup mm
 * \brief fork a vm space into another vm space
 */
int vm_space_fork(struct vm_space *src, struct vm_space *dst)
{
    if (!src || !dst)
        return -EINVAL;

    /* copy vm entries */
    queue_for (node, &src->vm_entries) {
        struct vm_entry *s_entry = node->value;
        struct vm_entry *d_entry = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, 0);

        if (!d_entry) {
            /* TODO */
        }

        memcpy(d_entry, s_entry, sizeof(struct vm_entry));
        d_entry->qnode = enqueue(&dst->vm_entries, d_entry);

        if (s_entry->vm_anon) {
            s_entry->vm_anon->flags |= VM_COPY;
            vm_anon_incref(s_entry->vm_anon);
            //s_entry->vm_anon->ref++;
        }

        if (s_entry->vm_object) {
            s_entry->vm_object->ref++;
        }

        if (s_entry->flags & (VM_UW|VM_KW) && !(s_entry->flags & VM_SHARED)) {
            /* remove write permission from all pages */
            vaddr_t sva = s_entry->base;
            vaddr_t eva = sva + s_entry->size;
            unsigned flags = s_entry->flags & ~(VM_UW|VM_KW);
            pmap_protect(src->pmap, sva, eva, flags);
        }
    }

    return 0;
}
