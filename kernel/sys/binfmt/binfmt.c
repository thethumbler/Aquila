#include <core/arch.h>
#include <sys/binfmt.h>
#include <sys/elf.h>
#include <mm/vm.h>

static struct binfmt binfmt_list[] = {
    {binfmt_elf_check, binfmt_elf_load},
};

#define NR_BINFMT   ((sizeof(binfmt_list)/sizeof(binfmt_list[0])))

static int binfmt_fmt_load(struct proc *proc, const char *path, struct vnode *vnode, struct binfmt *binfmt, struct proc **ref)
{
    int err = 0;

    /* point of no return: the old image is gone after this, on failure
     * the vm space is left empty and the caller has to kill the process */
    vm_space_destroy(&proc->vm_space);

    if ((err = binfmt->load(proc, path, vnode)))
        goto error;

    kfree(proc->name);
    proc->name = strdup(path);

    /* Align heap */
    proc->heap_start = PAGE_ROUND(proc->heap_start);
    proc->heap = proc->heap_start;

    /* Create heap vm_entry */
    struct vm_entry *heap_vm = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, M_ZERO);
    if (!heap_vm) {
        err = -ENOMEM;
        goto error;
    }

    heap_vm->base  = proc->heap_start;
    heap_vm->size  = 0;
    heap_vm->flags = VM_URW;
    heap_vm->qnode = enqueue(&proc->vm_space.vm_entries, heap_vm);

    heap_vm->vm_object = NULL; //vm_object_anon();
    //vm_object_incref(heap_vm->vm_object);
    
    proc->heap_vm  = heap_vm;

    /* Create stack vm_entry */
    struct vm_entry *stack_vm = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, M_ZERO);
    if (!stack_vm) {
        err = -ENOMEM;
        goto error;
    }

    stack_vm->base  = USER_STACK_BASE;
    stack_vm->size  = USER_STACK_SIZE;
    stack_vm->flags = VM_URW;
    stack_vm->qnode = enqueue(&proc->vm_space.vm_entries, stack_vm);

    stack_vm->vm_object = NULL; //vm_object_anon();
    //vm_object_incref(stack_vm->vm_object);

    proc->stack_vm  = stack_vm;

    struct thread *thread = (struct thread *) proc->threads.head->value;
    thread->stack_base = USER_STACK_BASE;
    thread->stack_size = USER_STACK_SIZE;

    return 0;

error:
    /* drop whatever was partially loaded */
    vm_space_destroy(&proc->vm_space);
    return err;
}

int binfmt_load(struct proc *proc, const char *path, struct proc **ref)
{
    struct vnode *vnode = NULL;
    int err = 0;

    struct uio uio;
    memset(&uio, 0, sizeof(struct uio));

    if (proc) {
        uio = PROC_UIO(proc);
    }

    if ((err = vfs_lookup(path, &uio, &vnode, NULL)))
        return err;

    if (!vnode)
        return -ENOENT;

    /* a regular file the process may execute */
    if (!S_ISREG(vnode->mode))
        return -EACCES;

    if ((err = vfs_access(vnode, &uio, VFS_X)))
        return err;

    for (size_t i = 0; i < NR_BINFMT; ++i) {
        if (!binfmt_list[i].check(vnode)) {
            err = binfmt_fmt_load(proc, path, vnode, &binfmt_list[i], ref);
            //vfs_close(vnode);
            return err;
        }
    }

    //vfs_close(vnode);
    return -ENOEXEC;
}
