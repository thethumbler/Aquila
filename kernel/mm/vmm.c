/** \file
 * Mapping and unmapping a map entry's range in an address space, and the
 * kernel's own address space (`kvm_space`).
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <ds/queue.h>
#include <ds/hashmap.h>

MALLOC_DEFINE(M_VM_AREF, "vm-aref", "anonymous virtual memory object reference");

/* The kernel's address space. */
struct vm_space kvm_space;

/**
 * Maps a map entry's whole range at once: its physical memory (`paddr`),
 * or new pages of memory if it has none. Entries whose pages come in on
 * page faults aren't mapped this way.
 *
 * @param vm_space  the address space
 * @param vm_entry  the entry
 * @return 0, or `-ENOMEM` if there was no memory for a page or a page
 *         table
 */
int vm_map(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    return mm_map(vm_space->pmap, vm_entry->paddr, vm_entry->base, vm_entry->size, vm_entry->flags);
}

/**
 * Unmaps every page of a map entry's range. The pages themselves stay
 * with what holds them (the anon, the file, the device): only the
 * mappings go.
 *
 * @param vm_space  the address space
 * @param vm_entry  the entry
 */
void vm_unmap_full(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    mm_unmap_full(vm_space->pmap, vm_entry->base, vm_entry->size);
}

