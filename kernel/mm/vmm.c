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

int mem_map(uintptr_t phys_addr, uintptr_t virt_addr, size_t size, int flags)
{
    if (phys_addr)
        return pmman.map_to(phys_addr, virt_addr, size, flags);
    else
        return pmman.map(virt_addr, size, flags);
}

int vm_map(uintptr_t phys_addr, struct vmr *vmr)
{
    return mem_map(phys_addr, vmr->base, vmr->size, vmr->flags);
}

void vm_unmap(struct vmr *vmr)
{
    pmman.unmap_full(vmr->base, vmr->size);
}
