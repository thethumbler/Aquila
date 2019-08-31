#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <ds/queue.h>
#include <ds/hashmap.h>

MALLOC_DEFINE(M_VM_AREF, "vm-aref", "anonymous virtual memory object reference");

struct vm_space kvm_space;

int vm_map(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    //printk("vm_map(vm_space=%p, vm_entry=%p)\n", vm_space, vm_entry);
    return mm_map(vm_space->pmap, vm_entry->paddr, vm_entry->base, vm_entry->size, vm_entry->flags);
}

void vm_unmap(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    printk("vm_unmap(vm_space=%p, vm_entry=%p)\n", vm_space, vm_entry);

    if (vm_entry->flags & VM_SHARED) {
        /* TODO */
    } else {
        mm_unmap(vm_space->pmap, vm_entry->base, vm_entry->size);
    }
}

void vm_unmap_full(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    printk("vm_unmap(vm_space=%p, vm_entry=%p)\n", vm_space, vm_entry);

    if (vm_entry->flags & VM_SHARED) {
        /* TODO */
    } else {
        mm_unmap_full(vm_space->pmap, vm_entry->base, vm_entry->size);
    }
}

