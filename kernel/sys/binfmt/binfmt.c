#include <core/arch.h>
#include <fs/vfs.h>
#include <sys/binfmt.h>
#include <sys/elf.h>
#include <mm/vm.h>

struct {
    int (*check)(struct inode *inode);
    int (*load)(proc_t *proc, struct inode *file);
} binfmt_list[] = {
    {binfmt_elf_check, binfmt_elf_load},
};

#define BINFMT_NR ((sizeof(binfmt_list)/sizeof(binfmt_list[0])))

static int binfmt_fmt_load(proc_t *proc, const char *fn, struct inode *file, int (*load)(proc_t *, struct inode *), proc_t **ref)
{
    int err = 0;

    void *arch_specific_data = NULL;
    int new_proc = !proc;

    if (new_proc) {
        arch_specific_data = arch_binfmt_load();
        if ((err = proc_new(&proc)))
            goto error;
    } else {
        /* Remove VMRs */
        struct vmr *vmr = NULL;
        while ((vmr = dequeue(&proc->vmr))) {
            vm_unmap_full(vmr);
            kfree(vmr);
        }

        kfree(proc->name);
    }

    if ((err = load(proc, file)))
        goto error;

    proc->name = strdup(fn);

    /* Align heap */
    proc->heap_start = UPPER_PAGE_BOUNDARY(proc->heap_start);
    proc->heap = proc->heap_start;

    /* Create heap VMR */
    struct vmr *heap_vmr = kmalloc(sizeof(struct vmr));
    memset(heap_vmr, 0, sizeof(struct vmr));
    heap_vmr->base  = proc->heap_start;
    heap_vmr->size  = 0;
    heap_vmr->flags = VM_URW | VM_ZERO;
    heap_vmr->qnode = enqueue(&proc->vmr, heap_vmr);
    proc->heap_vmr  = heap_vmr;

    /* Create stack VMR */
    struct vmr *stack_vmr = kmalloc(sizeof(struct vmr));
    memset(stack_vmr, 0, sizeof(struct vmr));
    stack_vmr->base  = USER_STACK_BASE;
    stack_vmr->size  = USER_STACK_SIZE;
    stack_vmr->flags = VM_URW;
    stack_vmr->qnode = enqueue(&proc->vmr, stack_vmr);
    //vm_map(stack_vmr);
    proc->stack_vmr  = stack_vmr;

    if (new_proc) {
        //pmman.map(USER_STACK_BASE, USER_STACK_SIZE, VM_URW);
        arch_proc_init(arch_specific_data, proc);
        arch_binfmt_end(arch_specific_data);

        if (ref)
            *ref = proc;
    }

    return 0;

error:
    arch_binfmt_end(arch_specific_data);
    return err;
}

int binfmt_load(proc_t *proc, const char *path, proc_t **ref)
{
    struct vnode v;
    struct inode *file = NULL;
    int err;

    struct uio uio = {0};

    if (proc) {
        uio = _PROC_UIO(proc);
    }

    if ((err = vfs_lookup(path, &uio, &v, NULL)))
        return err;

    if ((err = vfs_vget(&v, &file)))
        return err;

    for (size_t i = 0; i < BINFMT_NR; ++i) {
        if (!binfmt_list[i].check(file)) {
            binfmt_fmt_load(proc, path, file, binfmt_list[i].load, ref);
            vfs_close(file);
            return 0;
        }
    }

    vfs_close(file);
    return -ENOEXEC;
}

