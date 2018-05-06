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

int vm_map(struct vmr *vmr)
{
    return mm_map(vmr->paddr, vmr->base, vmr->size, vmr->flags);
}

void vm_unmap(struct vmr *vmr)
{
    mm_unmap(vmr->base, vmr->size);
}

void vm_unmap_full(struct vmr *vmr)
{
    mm_unmap_full(vmr->base, vmr->size);
}
