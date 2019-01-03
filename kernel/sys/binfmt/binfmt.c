#include <core/arch.h>
#include <sys/binfmt.h>
#include <sys/elf.h>
#include <mm/vm.h>

struct binfmt binfmt_list[] = {
    {binfmt_elf_check, binfmt_elf_load},
};

#define NR_BINFMT   ((sizeof(binfmt_list)/sizeof(binfmt_list[0])))

static int binfmt_fmt_load(struct proc *proc, const char *path, struct inode *inode, struct binfmt *binfmt, struct proc **ref)
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

    if ((err = binfmt->load(proc, path, inode)))
        goto error;

    proc->name = strdup(path);

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
    proc->stack_vmr  = stack_vmr;

    struct thread *thread = (struct thread *) proc->threads.head->value;
    thread->stack_base = USER_STACK_BASE;
    thread->stack_size = USER_STACK_BASE;

    if (new_proc) {
        arch_proc_init(arch_specific_data, proc);
        arch_binfmt_end(arch_specific_data);

        if (ref) *ref = proc;
    }

    return 0;

error:
    arch_binfmt_end(arch_specific_data);
    return err;
}

int binfmt_load(struct proc *proc, const char *path, struct proc **ref)
{
    struct vnode vnode;
    struct inode *inode = NULL;
    int err = 0;

    struct uio uio = {0};

    if (proc) {
        uio = PROC_UIO(proc);
    }

    if ((err = vfs_lookup(path, &uio, &vnode, NULL)))
        return err;

    if ((err = vfs_vget(&vnode, &inode)))
        return err;

    for (size_t i = 0; i < NR_BINFMT; ++i) {
        if (!binfmt_list[i].check(inode)) {
            binfmt_fmt_load(proc, path, inode, &binfmt_list[i], ref);
            vfs_close(inode);
            return 0;
        }
    }

    vfs_close(inode);
    return -ENOEXEC;
}
