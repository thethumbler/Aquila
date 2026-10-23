#include <core/system.h>
#include <core/string.h>
#include <mm/vm.h>
#include <mm/pmap.h>

static struct vm_pager vnode_pager;

static int vm_page_eq(void *_a, void *_b)
{
    struct vm_page *a = (struct vm_page *) _a;
    size_t *b = (size_t *) _b;

    return a->off == *b;
}

/**
 * create a new `vm_object` associated with a `vnode`
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


struct vm_page *vnode_page_in(struct vm_object *vm_object, size_t off)
{
    struct vnode *vnode = (struct vnode *) vm_object->p;

    off = PAGE_ALIGN(off);

    /* read into a buffer of its own: reading may sleep (on the disk); a
     * short read leaves zeros, not stale memory */
    char *buf = kmalloc(PAGE_SIZE, &M_BUFFER, M_ZERO);
    if (!buf) return NULL;

    /* a page that can't be read isn't mapped as zeros */
    if (vfs_read(vnode, off, PAGE_SIZE, buf) < 0) {
        kfree(buf);
        return NULL;
    }

    /* another thread may have paged it in meanwhile */
    hash_t hash = hashmap_digest(&off, sizeof(off));
    struct hashmap_node *node = hashmap_lookup(vm_object->pages, hash, &off);

    if (node) {
        kfree(buf);
        return (struct vm_page *) node->entry;
    }

    struct vm_page *vm_page = mm_page_alloc();

    if (!vm_page) {
        kfree(buf);
        return NULL;
    }

    vm_page->vm_object = vm_object;
    vm_page->off = off;
    vm_page->ref = 1;

    pmap_page_write(vm_page->paddr, 0, PAGE_SIZE, buf);
    kfree(buf);

    vm_object_page_insert(vm_object, vm_page);

    return vm_page;
}

/**
 * After a write or a truncation changed [`start', `end') of `vnode': its
 * pages in the page cache are read again. A page stays where it is,
 * mapped by every process that maps the file, so its contents change, not
 * the page; past the file's end it reads zeros. (A private mapping's
 * copies, made on a write, are its own and don't change.)
 */
void vnode_pages_update(struct vnode *vnode, off_t start, off_t end)
{
    struct vm_object *vm_object = vnode->vm_object;

    if (!vm_object || !vm_object->pages->count || end <= start)
        return;

    char *buf = NULL;

    /* looked up one by one, not walked: the read may sleep, and the map
     * change meanwhile (pages are never freed, a page found stays) */
    for (size_t off = PAGE_ALIGN((size_t) start); off < (size_t) end; off += PAGE_SIZE) {
        hash_t hash = hashmap_digest(&off, sizeof(off));
        struct hashmap_node *node = hashmap_lookup(vm_object->pages, hash, &off);

        if (!node)
            continue;

        struct vm_page *vm_page = (struct vm_page *) node->entry;

        if (!buf && !(buf = kmalloc(PAGE_SIZE, &M_BUFFER, 0))) {
            printk("vm: no memory to update a file's cached pages\n");
            return;
        }

        memset(buf, 0, PAGE_SIZE);

        if (vfs_read(vnode, off, PAGE_SIZE, buf) < 0)
            continue;

        pmap_page_write(vm_page->paddr, 0, PAGE_SIZE, buf);
    }

    kfree(buf);
}

/**
 * The vnode is being freed: its object and its cached pages go with it.
 * Nothing maps them: a mapping holds the vnode (vm_object_incref()).
 */
void vnode_pages_free(struct vnode *vnode)
{
    struct vm_object *vm_object = vnode->vm_object;

    if (!vm_object)
        return;

    hashmap_for (qnode, vm_object->pages) {
        struct hashmap_node *node = (struct hashmap_node *) qnode->value;
        struct vm_page *vm_page = (struct vm_page *) node->entry;

        mm_page_dealloc(vm_page->paddr);
    }

    hashmap_free(vm_object->pages);
    kfree(vm_object);
    vnode->vm_object = NULL;
}

static struct vm_pager vnode_pager = {
    .in = vnode_page_in,
    .out = NULL,
};
