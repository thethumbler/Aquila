#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>

#include <mm/pmap.h>
#include <mm/vm.h>

#include <sys/sched.h>
#include <sys/signal.h>

/**
 * a structure holding parameters relevant to a page fault
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
 * handle the page fault if the page is already present
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

        if (!new_page)
            return -ENOMEM;

        new_page->off = vm_aref->vm_page->off;
        new_page->ref = 1;
        new_page->vm_object = NULL;

        pmap_page_copy(vm_aref->vm_page->paddr, new_page->paddr);

        mm_page_decref(vm_aref->vm_page->paddr);

        vm_aref->vm_page = new_page;
        vm_aref->flags &= ~VM_COPY;

        /* no memory for its page table: the copy is the aref's, a fault
         * again maps it */
        int err = mm_page_map(pmap, pf->addr, new_page->paddr, pf->vm_entry->flags & VM_PERM);

        if (err)
            return err;
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

            if (!new_anon)
                return -ENOMEM;

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
        int err = mm_page_map(pmap, pf->addr, vm_page->paddr, perm);

        if (err)
            return err;

        mm_page_incref(vm_page->paddr);
        return 1;
    }

    /* we have PF_WRITE */

    if (aref->ref == 1) {
        /* we own the aref, just map */

        struct vm_page *vm_page = aref->vm_page;

        uint32_t perm = vm_entry->flags & VM_PERM;
        int err = mm_page_map(pmap, pf->addr, vm_page->paddr, perm);

        if (err)
            return err;

        mm_page_incref(vm_page->paddr);
        return 1;
    }

    /* copy, map read-write */

    /* the copy and its aref first: without memory for them, nothing has
     * changed */
    struct vm_aref *new_aref = kmalloc(sizeof(struct vm_aref), &M_VM_AREF, M_ZERO);
    struct vm_page *new_page = new_aref ? mm_page_alloc() : NULL;

    if (!new_page) {
        kfree(new_aref);
        return -ENOMEM;
    }

    struct vm_page *vm_page = aref->vm_page;

    new_page->off = vm_page->off;
    new_page->ref = 1;
    new_page->vm_object = NULL;

    pmap_page_copy(vm_page->paddr, new_page->paddr);

    new_aref->ref = 1;
    new_aref->flags = aref->flags;
    new_aref->vm_page = new_page;

    /* the anon's page at this offset is the copy (the same key: its node
     * takes the new aref) */
    aref_node->entry = new_aref;
    aref->ref--;

    /* no memory for its page table: the copy is the anon's now, a fault
     * again maps it */
    int err = mm_page_map(pmap, pf->addr, new_page->paddr, vm_entry->flags & VM_PERM);

    return err ? err : 1;
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
        if (vm_object->pager && vm_object->pager->in)
            vm_page = vm_object->pager->in(vm_object, off);
        else
            printk("vm: an object with no pager, page %p missing\n", (void *) off);
    }

    return vm_page;
}

static inline int pf_object(struct pf *pf)
{
    struct vm_entry *vm_entry = pf->vm_entry;

    struct vm_object *vm_object = vm_entry->vm_object;
    struct vm_page *vm_page = NULL;
    struct pmap *pmap = pf->vm_space->pmap;

    /* look for page in the object pages hashmap; NULL: it couldn't be
     * read in (no memory, a read error) */
    vm_page = vm_object_page(vm_object, pf->hash, pf->off);

    if (!vm_page)
        return -EIO;

    if (!(vm_entry->flags & VM_UW)) {
        /* read only page -- just map */
        int err = mm_page_map(pmap, pf->addr, vm_page->paddr, vm_entry->flags & VM_PERM);

        if (err)
            return err;

        mm_page_incref(vm_page->paddr);
        return 1;
    }

    /* read-write page -- promote */

    /* allocate a new anon if we don't have one */
    if (!vm_entry->vm_anon) {
        if (!(vm_entry->vm_anon = vm_anon_new()))
            return -ENOMEM;

        vm_entry->vm_anon->ref = 1;
    }

    struct vm_aref *vm_aref = kmalloc(sizeof(struct vm_aref), &M_VM_AREF, M_ZERO);

    if (!vm_aref)
        return -ENOMEM;

    vm_aref->ref = 1;

    /* copy page */
    struct vm_page *new_page = mm_page_alloc();

    if (!new_page) {
        kfree(vm_aref);
        return -ENOMEM;
    }

    new_page->off = vm_page->off;
    new_page->ref = 1;
    new_page->vm_object = NULL;

    pmap_page_copy(vm_page->paddr, new_page->paddr);

    vm_aref->vm_page = new_page;

    if (hashmap_insert(vm_entry->vm_anon->arefs, pf->hash, vm_aref)) {
        mm_page_dealloc(new_page->paddr);
        kfree(vm_aref);
        return -ENOMEM;
    }

    /* no memory for its page table: the copy is the anon's now, a fault
     * again maps it */
    int err = mm_page_map(pmap, pf->addr, new_page->paddr, vm_entry->flags & VM_PERM);

    return err ? err : 1;
}

static inline int pf_zero(struct pf *pf)
{
    struct vm_entry *vm_entry = pf->vm_entry;
    struct pmap *pmap = pf->vm_space->pmap;

    if (!vm_entry->vm_anon) {
        if (!(vm_entry->vm_anon = vm_anon_new()))
            return -ENOMEM;

        vm_entry->vm_anon->ref = 1;
    }

    struct vm_aref *vm_aref = kmalloc(sizeof(struct vm_aref), &M_VM_AREF, M_ZERO);

    if (!vm_aref)
        return -ENOMEM;

    struct vm_page *new_page = mm_page_alloc();

    if (!new_page) {
        kfree(vm_aref);
        return -ENOMEM;
    }

    new_page->off = pf->off;
    new_page->ref = 1;

    vm_aref->vm_page = new_page;
    vm_aref->ref = 1;

    /* zeroed through its mapping (the kernel's, for now), before the anon
     * has it: a page that isn't zeroed is never anyone's */
    int err = mm_page_map(pmap, pf->addr, new_page->paddr, VM_KW);

    if (!err) {
        memset((void *) pf->addr, 0, PAGE_SIZE);
        pmap_protect(pmap, pf->addr, pf->addr + PAGE_SIZE, vm_entry->flags & VM_PERM);
        err = hashmap_insert(vm_entry->vm_anon->arefs, pf->hash, vm_aref);

        if (err)
            pmap_remove(pmap, pf->addr, pf->addr + PAGE_SIZE);
    }

    if (err) {
        mm_page_dealloc(new_page->paddr);
        kfree(vm_aref);
        return err;
    }

    return 1;
}

/**
 * handle a page fault
 *
 * This function is called from the arch subsystem to handle
 * a page fault
 *
 * \param vaddr the virtual address that triggered the page fault
 * \param flags the flags associated with page fault
 * \return 0 if it's resolved (the page is there now), -EFAULT if not:
 * the architecture decides (a signal, a fixup, a panic)
 */
int mm_page_fault(vaddr_t vaddr, int flags)
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

    /* each step: 1, the page is there now; 0, not this step's to handle;
     * < 0, it can't be (no memory, the file's page couldn't be read): the
     * same as an address that isn't the process's */
    int ret = 0;

    /* try to handle page present case */
    if (flags & PF_PRESENT && (ret = pf_present(&pf)))
        goto done;

    /* check the anon layer for the page and handle if present */
    if (vm_entry->vm_anon && (ret = pf_anon(&pf)))
        goto done;

    /* check the backening object for the page and handle if present */
    if (vm_entry->vm_object && (ret = pf_object(&pf)))
        goto done;

    /* just zero out the page */
    ret = pf_zero(&pf);

done:
    if (ret > 0)
        return 0;

    if (ret < 0)
        printk("[%d:%d] %s: page fault at %p not resolved: error %d\n",
               curproc->pid, curthread->tid, curproc->name, (void *) vaddr, -ret);

    /* out of memory: the process can't go on (the access would fault
     * again), it's killed; a copy from or to its memory in a system call
     * fails (EFAULT), and it's killed on its way back */
    if (ret == -ENOMEM)
        signal_proc_send(curproc, SIGKILL);

sigsegv:
    curthread->fault_addr = vaddr;
    return -EFAULT;
}
