#include <core/system.h>
#include <mm/vm.h>

static struct vm_pager vnode_pager;

static int vm_page_eq(void *_a, void *_b)
{
    struct vm_page *a = (struct vm_page *) _a;
    size_t *b = (size_t *) _b;

    return a->off == *b;
}

/**
 * \ingroup vfs
 * \brief create a new `vm_object` associated with a `vnode`
 */
struct vm_object *vm_object_vnode(struct vnode *vnode)
{
    if (!vnode)
        return NULL;

    if (!vnode->vm_object) {
        struct vm_object *vm_object;

        vm_object = kmalloc(sizeof(struct vm_object), &M_VM_OBJECT, M_ZERO);
        if (!vm_object) return NULL;

        vm_object->type  = VMOBJ_FILE;
        vm_object->pages = hashmap_new(0, vm_page_eq);

        if (!vm_object->pages) {
            kfree(vm_object);
            return NULL;
        }

        vm_object->pager = &vnode_pager;
        vm_object->p = vnode;

        vnode->vm_object = vm_object;
    }

    return vnode->vm_object;
}


static char __load[PAGE_SIZE] __aligned(PAGE_SIZE);
struct vm_page *vnode_page_in(struct vm_object *vm_object, size_t off)
{
    struct vm_page *vm_page = mm_page_alloc();
    if (!vm_page) return NULL;

    vm_page->vm_object = vm_object;
    vm_page->off = PAGE_ALIGN(off);
    vm_page->ref = 1;

    struct vnode *vnode = (struct vnode *) vm_object->p;

    mm_page_map(kvm_space.pmap, (vaddr_t) __load, vm_page->paddr, VM_KW);
    vfs_read(vnode, vm_page->off, PAGE_SIZE, (void *) __load);

    vm_object_page_insert(vm_object, vm_page);

    return vm_page;
}

static struct vm_pager vnode_pager = {
    .in = vnode_page_in,
    .out = NULL,
};
