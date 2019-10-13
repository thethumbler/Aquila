#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <ds/queue.h>
#include <ds/hashmap.h>

MALLOC_DEFINE(M_VM_OBJECT, "vm-object", "virtual memory object");

void vm_object_incref(struct vm_object *vm_object)
{
    vm_object->ref++;
}

void vm_object_decref(struct vm_object *vm_object)
{
    vm_object->ref--;
    return;

    /*
    if (vm_object->ref == 0) {
        hashmap_free(vm_object->pages);
        kfree(vm_object);
    }
    */
}

void vm_object_page_insert(struct vm_object *vm_object, struct vm_page *vm_page)
{
    size_t off = vm_page->off;
    hash_t hash = hashmap_digest(&off, sizeof(off));
    hashmap_insert(vm_object->pages, hash, vm_page);
}
