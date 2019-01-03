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

int vm_map(struct vmr *vmr)
{
    return mm_map(vmr->paddr, vmr->base, vmr->size, vmr->flags);
}

void vm_unmap(struct vmr *vmr)
{
    if (vmr->flags & VM_SHARED) {
        /* TODO */
    } else {
        mm_unmap(vmr->base, vmr->size);
    }
}

void vm_unmap_full(struct vmr *vmr)
{
    if (vmr->flags & VM_SHARED) {
        /* TODO */
    } else {
        mm_unmap_full(vmr->base, vmr->size);
    }
}

int vm_vmr_insert(struct queue *queue, struct vmr *vmr)
{
    uintptr_t end = vmr->base + vmr->size;

    struct qnode *cur = NULL;
    uintptr_t prev_end = 0;

    forlinked (node, queue->head, node->next) {
        struct vmr *cur_vmr = (struct vmr *) node->value;
        if (cur_vmr->base >= end && prev_end <= vmr->base) {
            cur = node;
            break;
        }
        prev_end = cur_vmr->base + cur_vmr->size;
    }

    if (!cur)
        return -ENOMEM;

    struct qnode *node = kmalloc(sizeof(struct qnode));
    node->value = vmr;
    node->next = cur;
    node->prev = cur->prev;

    if (cur->prev)
        cur->prev->next = node;

    cur->prev  = node;
    vmr->qnode = node;
    ++queue->count;

    return 0;
}
