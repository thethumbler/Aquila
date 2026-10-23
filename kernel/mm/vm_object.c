#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <ds/queue.h>
#include <ds/hashmap.h>
#include <fs/vfs.h>

MALLOC_DEFINE(M_VM_OBJECT, "vm-object", "virtual memory object");

/**
 * One more mapping of the object. A file's object holds its vnode while
 * it's mapped: the pages are read from it, and the program it runs may be
 * removed meanwhile.
 */
void vm_object_incref(struct vm_object *vm_object)
{
    if (!vm_object->ref++ && vm_object->type == VMOBJ_FILE)
        ((struct vnode *) vm_object->p)->ref++;
}

/**
 * One mapping of the object less. A file's object lets its vnode go with
 * the last one; its pages stay, cached with the vnode, until the vnode is
 * freed (vnode_pages_free()).
 */
void vm_object_decref(struct vm_object *vm_object)
{
    if (!--vm_object->ref && vm_object->type == VMOBJ_FILE)
        vfs_close((struct vnode *) vm_object->p);
}

void vm_object_page_insert(struct vm_object *vm_object, struct vm_page *vm_page)
{
    size_t off = vm_page->off;
    hash_t hash = hashmap_digest(&off, sizeof(off));
    hashmap_insert(vm_object->pages, hash, vm_page);
}
