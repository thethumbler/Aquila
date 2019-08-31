#include <core/system.h>
#include <core/panic.h>
#include <mm/vm.h>

MALLOC_DEFINE(M_VM_ANON, "vm-anon", "anonymous virtual memory object");

/** check if two arefs are equal in a hashmap
 */
static int vm_aref_eq(void *_a, void *_b)
{
    struct vm_aref *a = (struct vm_aref *) _a;

    if (!a->vm_page)
        return -1;

    size_t *b = (size_t *) _b;

    return a->vm_page->off == *b;
}

/** create new anon structure
 */
struct vm_anon *vm_anon_new(void)
{
    struct vm_anon *vm_anon = kmalloc(sizeof(struct vm_anon), &M_VM_ANON, 0);

    if (!vm_anon)
        goto error;

    memset(vm_anon, 0, sizeof(struct vm_anon));
    vm_anon->arefs = hashmap_new(0, vm_aref_eq);

    if (!vm_anon->arefs)
        goto error;

    return vm_anon;

error:
    panic("failed to allocate vm_anon");

    if (vm_anon) {
        if (vm_anon->arefs)
            hashmap_free(vm_anon->arefs);
        kfree(vm_anon);
    }

    return NULL;
}

void vm_aref_destroy(struct vm_aref *vm_aref)
{
    //printk("vm_aref_destroy(vm_aref=%p)\n", vm_aref);
}

void vm_aref_decref(struct vm_aref *vm_aref)
{
    vm_aref->ref--;

    /*
    if (!vm_aref->ref) {
        //printk("should free vm_aref %p\n", vm_aref);
        kfree(vm_aref);
    }
    */
}

void vm_anon_destroy(struct vm_anon *vm_anon)
{
    if (!vm_anon) return;

    struct hashmap *arefs = vm_anon->arefs;

    if (!arefs)
        return;

    hashmap_for (qnode, vm_anon->arefs) {
        struct hashmap_node *hnode = (struct hashmap_node *) qnode->value;
        struct vm_aref *aref = (struct vm_aref *) hnode->entry;

        vm_aref_decref(aref);

        if (!aref->ref) {
            if (aref->vm_page) {
                mm_page_dealloc(aref->vm_page->paddr);
            }

            kfree(aref);
        }
    }

    hashmap_free(vm_anon->arefs);
}

/** increment number of references to a vm anon
 */
void vm_anon_incref(struct vm_anon *vm_anon)
{
    if (!vm_anon)
        return;

    vm_anon->ref++;
}

/** decrement number of references to a vm anon
 * and destroy it when it reaches zero
 */
void vm_anon_decref(struct vm_anon *vm_anon)
{
    if (!vm_anon)
        return;

    vm_anon->ref--;

    if (!vm_anon->ref) {
        vm_anon_destroy(vm_anon);
        kfree(vm_anon);
    }
}

/** copy all aref structures from `src` to `dst`
 */
static int vm_anon_copy_arefs(struct vm_anon *src, struct vm_anon *dst)
{
    if (!src || !dst || !src->arefs || !dst->arefs)
        return -EINVAL;

    /* copy all arefs */
    hashmap_for (qnode, src->arefs) {
        struct hashmap_node *node = (struct hashmap_node *) qnode->value;
        struct vm_aref *aref = (struct vm_aref *) node->entry;

        hashmap_insert(dst->arefs, node->hash, aref);
        aref->ref++;
    }

    return 0;
}

/** clone an existing anon into a new anon
 */
struct vm_anon *vm_anon_copy(struct vm_anon *vm_anon)
{
    if (!vm_anon)
        return NULL;

    struct vm_anon *new_anon = vm_anon_new();

    if (!new_anon)
        return NULL;

    new_anon->flags = vm_anon->flags & ~VM_COPY;
    new_anon->ref   = 1;

    /* copy all arefs */
    if (vm_anon_copy_arefs(vm_anon, new_anon)) {
        kfree(new_anon);
        return NULL;
    }

    return new_anon;
}
