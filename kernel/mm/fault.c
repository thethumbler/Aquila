#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>

#include <mm/pmap.h>
#include <mm/vm.h>

#include <sys/sched.h>

/**
 * \ingroup mm
 * \brief a structure holding parameters relevant to a page fault
 */
struct pf {
    int flags;
    vaddr_t addr;

    struct vm_space *vm_space;
    struct vm_entry *vm_entry;

    size_t off;
    hash_t hash;
};

static inline int check_violation(int flags, int vm_flags)
{
    /* returns 1 on violation, 0 otherwise */
    return
    ((flags & PF_READ)  && !(vm_flags & VM_UR)) ||
    ((flags & PF_WRITE) && !(vm_flags & VM_UW)) ||
    ((flags & PF_EXEC)  && !(vm_flags & VM_UX));
}

/**
 * \ingroup mm
 * \brief handle the page fault if the page is already present
 * in virtual memory mapping
 */
static inline int pf_present(struct pf *pf)
{
    /* page is already present but with incorrect permissions */
    struct vm_entry *vm_entry = pf->vm_entry;
    struct vm_anon  *vm_anon  = vm_entry->vm_anon;

    struct pmap *pmap = pf->vm_space->pmap;

    /* if there is no anon or the anon is shared
     * we can't handle it here and have to fallthrough to 
     * other handlers
     */
    if (!vm_anon || vm_anon->ref != 1)
        return 0;

    /* we own the anon */
    struct hashmap_node *hash_node = hashmap_lookup(vm_anon->arefs, pf->hash, &pf->off);
    struct vm_aref *vm_aref = (struct vm_aref *) (hash_node? hash_node->entry : NULL);

    if (!vm_aref || vm_aref->ref != 1)
        return 0;

    if (vm_aref->flags & VM_COPY) {
        /* copy page */
        struct vm_page *new_page = mm_page_alloc();
        new_page->off = vm_aref->vm_page->off;
        new_page->ref = 1;
        new_page->vm_object = NULL;

        pmap_page_copy(vm_aref->vm_page->paddr, new_page->paddr);

        mm_page_decref(vm_aref->vm_page->paddr);

        vm_aref->vm_page = new_page;
        vm_aref->flags &= ~VM_COPY;

        mm_page_map(pmap, pf->addr, new_page->paddr, pf->vm_entry->flags & VM_PERM);
    } else {
        /* we own the aref, just change permissions */
        pmap_protect(pmap, pf->addr, pf->addr+PAGE_SIZE, pf->vm_entry->flags & VM_PERM);
    }

    return 1;
}

static inline int pf_anon(struct pf *pf)
{
    struct vm_entry *vm_entry = pf->vm_entry;
    struct vm_anon  *vm_anon  = vm_entry->vm_anon;

    struct pmap *pmap = pf->vm_space->pmap;

    if (vm_anon->flags & VM_COPY) {

        if (vm_anon->ref > 1) {
            struct vm_anon *new_anon = vm_anon_copy(vm_anon);
            vm_anon_decref(vm_anon);
            vm_entry->vm_anon = new_anon;
            vm_anon = new_anon;
        }

        vm_anon->flags &= ~VM_COPY;
    }

    struct hashmap_node *aref_node = hashmap_lookup(vm_anon->arefs, pf->hash, &pf->off);

    if (!aref_node || !aref_node->entry)
        return 0;

    struct vm_aref *aref = (struct vm_aref *) aref_node->entry;

    if (!aref->vm_page)
        panic("aref has no page");

    if (!(pf->flags & PF_WRITE)) {
        /* map read-only */
        struct vm_page *vm_page = aref->vm_page;

        uint32_t perm = (vm_entry->flags & VM_PERM) & ~(VM_UW|VM_KW);
        mm_page_map(pmap, pf->addr, vm_page->paddr, perm);
        mm_page_incref(vm_page->paddr);

        return 1;
    }

    /* we have PF_WRITE */

    if (aref->ref == 1) {
        /* we own the aref, just map */

        struct vm_page *vm_page = aref->vm_page;

        uint32_t perm = vm_entry->flags & VM_PERM;
        mm_page_map(pmap, pf->addr, vm_page->paddr, perm);

        mm_page_incref(vm_page->paddr);

        return 1;
    }

    /* copy, map read-write */

    struct vm_aref *new_aref = kmalloc(sizeof(struct vm_aref), &M_VM_AREF, M_ZERO);

    if (!new_aref) {
        /* TODO */
    }

    new_aref->ref = 1;
    new_aref->flags = aref->flags;

    aref->ref--;

    struct vm_page *vm_page = aref->vm_page;

    struct vm_page *new_page = mm_page_alloc();
    new_page->off = vm_page->off;
    new_page->ref = 1;
    new_page->vm_object = NULL;

    pmap_page_copy(vm_page->paddr, new_page->paddr);

    new_aref->vm_page = new_page;

    hashmap_node_remove(vm_entry->vm_anon->arefs, aref_node);
    hashmap_insert(vm_entry->vm_anon->arefs, pf->hash, new_aref);

    mm_page_map(pmap, pf->addr, new_page->paddr, vm_entry->flags & VM_PERM);

    return 1;
}

static inline struct vm_page *vm_object_page(struct vm_object *vm_object, hash_t hash, size_t off)
{
    struct hashmap_node *hash_node = hashmap_lookup(vm_object->pages, hash, &off);
    struct vm_page *vm_page = NULL;

    if (hash_node) {
        /* page was found in the vm object */
        vm_page = (struct vm_page *) hash_node->entry;
    } else {
        /* page was not found, page in */
        if (vm_object->pager && vm_object->pager->in) {
            vm_page = vm_object->pager->in(vm_object, off);
        } else {
            /* what should we do now? */
            panic("Shit");
        }
    }

    return vm_page;
}

static inline int pf_object(struct pf *pf)
{
    struct vm_entry *vm_entry = pf->vm_entry;

    struct vm_object *vm_object = vm_entry->vm_object;
    struct vm_page *vm_page = NULL;
    struct pmap *pmap = pf->vm_space->pmap;

    /* look for page in the object pages hashmap */
    vm_page = vm_object_page(vm_object, pf->hash, pf->off);

    if (!(vm_entry->flags & VM_UW)) {
        /* read only page -- just map */
        mm_page_incref(vm_page->paddr);
        mm_page_map(pmap, pf->addr, vm_page->paddr, vm_entry->flags & VM_PERM);
        return 1;
    }

    /* read-write page -- promote */

    /* allocate a new anon if we don't have one */
    if (!vm_entry->vm_anon) {
        vm_entry->vm_anon = vm_anon_new();
        vm_entry->vm_anon->ref = 1;
    }

    struct vm_aref *vm_aref = kmalloc(sizeof(struct vm_aref), &M_VM_AREF, M_ZERO);

    if (!vm_aref) {
        /* TODO */
    }

    vm_aref->vm_page = vm_page;
    vm_aref->ref = 1;

    //if (!(pf->flags & PF_WRITE)) {
    //    /* just mark for copying */
    //    vm_aref->flags |= VM_COPY;
    //    hashmap_insert(vm_entry->vm_anon->arefs, pf->hash, vm_aref);
    //    uint32_t perms = (vm_entry->flags & VM_PERM) & ~(VM_UW|VM_KW);
    //    mm_page_map(pmap, pf->addr, vm_page->paddr, perms);
    //    return 1;
    //}

    /* copy page */
    struct vm_page *new_page = mm_page_alloc();
    new_page->off = vm_page->off;
    new_page->ref = 1;
    new_page->vm_object = NULL;

    pmap_page_copy(vm_page->paddr, new_page->paddr);

    vm_aref->vm_page = new_page;
    hashmap_insert(vm_entry->vm_anon->arefs, pf->hash, vm_aref);

    mm_page_map(pmap, pf->addr, new_page->paddr, vm_entry->flags & VM_PERM);


    return 1;
}

static inline int pf_zero(struct pf *pf)
{
    struct vm_entry *vm_entry = pf->vm_entry;
    struct pmap *pmap = pf->vm_space->pmap;

    if (!vm_entry->vm_anon) {
        vm_entry->vm_anon = vm_anon_new();
        vm_entry->vm_anon->ref = 1;
    }

    struct vm_aref *vm_aref = kmalloc(sizeof(struct vm_aref), &M_VM_AREF, M_ZERO);
    if (!vm_aref) {
        /* TODO */
    }

    struct vm_page *new_page = mm_page_alloc();
    new_page->off = pf->off;
    new_page->ref = 1;
    //new_page->vm_object = vm_object;
    
    vm_aref->vm_page = new_page;
    vm_aref->ref = 1;

    hashmap_insert(vm_entry->vm_anon->arefs, pf->hash, vm_aref);

    mm_page_map(pmap, pf->addr, new_page->paddr, VM_KW); //vm_entry->flags & VM_PERM);
    memset((void *) pf->addr, 0, PAGE_SIZE);
    pmap_protect(pmap, pf->addr, pf->addr+PAGE_SIZE, vm_entry->flags & VM_PERM);

    return 1;
}

/**
 * \ingroup mm
 * \brief handle a page fault
 *
 * This function is called from the arch subsystem to handle
 * a page fault
 *
 * \param vaddr the virtual address that triggered the page fault
 * \param flags the flags associated with page fault
 */
void mm_page_fault(vaddr_t vaddr, int flags)
{
    vaddr_t addr = PAGE_ALIGN(vaddr);

    struct vm_space *vm_space = &curproc->vm_space;
    struct pmap *pmap = vm_space->pmap;
    struct vm_entry *vm_entry = NULL;

    /* look for vm_entry that contains the page */
    vm_entry = vm_space_find(vm_space, addr);

    /* segfault if there is no entry or the permissions are incorrect */
    if (!vm_entry || check_violation(flags, vm_entry->flags))
        goto sigsegv;

    /* get page offset in object */
    size_t off = addr - vm_entry->base + vm_entry->off;

    /* hash the offset */
    hash_t hash = hashmap_digest(&off, sizeof(off));

    /* construct page fault structure */
    struct pf pf = {
        .flags = flags,
        .addr = addr,
        .vm_space = vm_space,
        .vm_entry = vm_entry,
        .off = off,
        .hash = hash,
    };

    /* try to handle page present case */
    if (flags & PF_PRESENT && pf_present(&pf))
        return;

    /* check the anon layer for the page and handle if present */
    if (vm_entry->vm_anon && pf_anon(&pf))
        return;

    /* check the backening object for the page and handle if present */
    if (vm_entry->vm_object && pf_object(&pf))
        return;

    /* just zero out the page */
    if (pf_zero(&pf))
        return;

sigsegv:
    signal_proc_send(curproc, SIGSEGV);
    return;
}
