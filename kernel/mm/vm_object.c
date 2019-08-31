#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <ds/queue.h>
#include <ds/hashmap.h>

MALLOC_DEFINE(M_VM_OBJECT, "vm-object", "virtual memory object");

static char __load[PAGE_SIZE] __aligned(PAGE_SIZE);


void vm_object_incref(struct vm_object *vm_object)
{
    vm_object->ref++;

#if 0
    if (vm_object->type == VMOBJ_SHADOW) {
        vm_object_incref(vm_object->shadow);
    }
#endif
}

void vm_object_decref(struct vm_object *vm_object)
{
    vm_object->ref--;
    return;

#if 0
    if (vm_object->type == VMOBJ_SHADOW) {
        vm_object_decref(vm_object->shadow);
    }
#endif

    if (vm_object->ref == 0) {
#if 0
        if (vm_object->type == VMOBJ_FILE)
            vm_object->inode->vm_object = NULL;
#endif

        hashmap_free(vm_object->pages);
        kfree(vm_object);
    }
}

void vm_object_page_insert(struct vm_object *vm_object, struct vm_page *vm_page)
{
    size_t off = vm_page->off;
    hash_t hash = hashmap_digest(&off, sizeof(off));
    hashmap_insert(vm_object->pages, hash, vm_page);
}

static int vm_page_eq(void *_a, void *_b)
{
    struct vm_page *a = (struct vm_page *) _a;
    size_t *b = (size_t *) _b;

    return a->off == *b;
}

struct vm_page *inode_page_in(struct vm_object *vm_object, size_t off)
{
    struct vm_page *vm_page = mm_page_alloc();

    if (!vm_page) return NULL;

    vm_page->vm_object = vm_object;
    vm_page->off = PAGE_ALIGN(off);
    vm_page->ref = 1;

    struct inode *inode = (struct inode *) vm_object->p;

    mm_page_map(kvm_space.pmap, (vaddr_t) __load, vm_page->paddr, VM_KW);
    vfs_read(inode, vm_page->off, PAGE_SIZE, (void *) __load);

    vm_object_page_insert(vm_object, vm_page);

    return vm_page;
}

struct vm_pager inode_pager = {
    .in = inode_page_in,
    .out = NULL,
};

struct vm_object *vm_object_inode(struct inode *inode)
{
    if (!inode)
        return NULL;

    if (!inode->vm_object) {
        //printk("vm_object_inode(%p)\n", inode);

        struct vm_object *vm_object;

        vm_object = kmalloc(sizeof(struct vm_object), &M_VM_OBJECT, 0);
        memset(vm_object, 0, sizeof(struct vm_object));

        vm_object->type  = VMOBJ_FILE;
        vm_object->pages = hashmap_new(0, vm_page_eq);

        if (!vm_object->pages) {
            kfree(vm_object);
            return NULL;
        }

        vm_object->pager = &inode_pager;
        vm_object->p = inode;

        inode->vm_object = vm_object;
    }

    return inode->vm_object;
}
