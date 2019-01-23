/**********************************************************************
 *                  Virtual Memory Manager
 *
 *
 *  This file is part of AquilaOS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */

#include <core/system.h>
#include <core/panic.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <ds/queue.h>

struct vm_space kvm_space;

int vm_map(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    //printk("vm_map(vm_space=%p, vm_entry=%p)\n", vm_space, vm_entry);

    return mm_map(vm_space->pmap, vm_entry->paddr, vm_entry->base, vm_entry->size, vm_entry->flags);
}

void vm_unmap(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    if (vm_entry->flags & VM_SHARED) {
        /* TODO */
    } else {
        mm_unmap(vm_space->pmap, vm_entry->base, vm_entry->size);
    }
}

void vm_unmap_full(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    if (vm_entry->flags & VM_SHARED) {
        /* TODO */
    } else {
        mm_unmap_full(vm_space->pmap, vm_entry->base, vm_entry->size);
    }
}

int vm_entry_insert(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    struct queue *queue = &vm_space->vm_entries;

    uintptr_t end = vm_entry->base + vm_entry->size;

    struct qnode *cur = NULL;
    uintptr_t prev_end = 0;

    forlinked (node, queue->head, node->next) {
        struct vm_entry *cur_vm_entry = (struct vm_entry *) node->value;
        if (cur_vm_entry->base >= end && prev_end <= vm_entry->base) {
            cur = node;
            break;
        }
        prev_end = cur_vm_entry->base + cur_vm_entry->size;
    }

    if (!cur)
        return -ENOMEM;

    struct qnode *node = kmalloc(sizeof(struct qnode));
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

void vm_space_destroy(struct vm_space *vm_space)
{
    struct vm_entry *vm_entry = NULL;
    while ((vm_entry = dequeue(&vm_space->vm_entries))) {
        vm_unmap_full(vm_space, vm_entry);
        kfree(vm_entry);
    }
}
