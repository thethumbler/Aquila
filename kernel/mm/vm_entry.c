#include <core/system.h>
#include <mm/vm.h>

MALLOC_DEFINE(M_VM_ENTRY, "vm-entry", "virtual memory map entry");

/** create new vm entry
 */
struct vm_entry *vm_entry_new(void)
{
    struct vm_entry *vm_entry = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, M_ZERO);
    if (!vm_entry) return NULL;

    return vm_entry;
}

/** destroy all resources associated with a vm entry
 */
void vm_entry_destroy(struct vm_entry *vm_entry)
{
    if (!vm_entry)
        return;

    if (vm_entry->vm_anon)
        vm_anon_decref(vm_entry->vm_anon);

    if (vm_entry->vm_object)
        vm_object_decref(vm_entry->vm_object);
}
