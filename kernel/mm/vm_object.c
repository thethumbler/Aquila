/** \file
 * Vm objects: what a mapping's pages come from, such as a file. A file's
 * object is its vnode's, and keeps the file's pages that have been read in
 * (the page cache).
 */

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
 * Adds a reference to a vm object: one more mapping of it. A file's object
 * holds its vnode while it's mapped, because the pages are read from it,
 * and the program file it runs may be removed meanwhile.
 *
 * @param vm_object  the object
 */
void vm_object_incref(struct vm_object *vm_object)
{
    if (!vm_object->ref++ && vm_object->type == VMOBJ_FILE)
        ((struct vnode *) vm_object->p)->ref++;
}

/**
 * Drops a reference to a vm object: one mapping of it less. A file's
 * object lets go of its vnode with the last one. Its pages stay, cached
 * with the vnode, until the vnode is freed (`vnode_pages_free()`).
 *
 * @param vm_object  the object
 */
void vm_object_decref(struct vm_object *vm_object)
{
    if (!--vm_object->ref && vm_object->type == VMOBJ_FILE)
        vfs_close((struct vnode *) vm_object->p);
}

/**
 * Adds a page to a vm object, at the page's offset (`vm_page->off`).
 *
 * @param vm_object  the object
 * @param vm_page    the page
 */
void vm_object_page_insert(struct vm_object *vm_object, struct vm_page *vm_page)
{
    size_t off = vm_page->off;
    hash_t hash = hashmap_digest(&off, sizeof(off));
    hashmap_insert(vm_object->pages, hash, vm_page);
}
