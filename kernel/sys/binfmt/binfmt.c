/** \file
 * Loading a program into a process, for `execve()`.
 *
 * The program's file is looked up and checked, then each executable
 * format the kernel knows (only ELF) is asked whether the file is one of
 * its own; the first that says yes loads it. Then the process gets its
 * heap, its stack and the signal code page.
 */

#include <core/arch.h>
#include <sys/binfmt.h>
#include <sys/elf.h>
#include <mm/vm.h>

static struct binfmt binfmt_list[] = {
    {binfmt_elf_check, binfmt_elf_load},
};

#define NR_BINFMT   ((sizeof(binfmt_list)/sizeof(binfmt_list[0])))

/**
 * Replaces a process's memory with a program, loaded by a format: the
 * process's other threads end, its old address space goes, and the
 * program, a heap, a stack and the signal code page are mapped. Returns 0
 * or a negative errno; on failure the address space is left empty, and
 * the caller has to kill the process.
 */
static int binfmt_fmt_load(struct proc *proc, const char *path, struct vnode *vnode, struct binfmt *binfmt, struct proc **ref)
{
    int err = 0;

    /* the point of no return: after this the old program is gone, and on
     * failure the address space is left empty. The other threads, which
     * run in it, end first */
    proc_threads_exec(proc);
    vm_space_destroy(&proc->vm_space);

    if ((err = binfmt->load(proc, path, vnode)))
        goto error;

    char *name = strdup(path);

    if (!name) {
        err = -ENOMEM;
        goto error;
    }

    kfree(proc->name);
    proc->name = name;

    /* the heap starts on a page boundary after the program */
    proc->heap_start = PAGE_ROUND(proc->heap_start);
    proc->heap = proc->heap_start;

    /* the heap's mapping, empty: sbrk() grows it */
    struct vm_entry *heap_vm = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, M_ZERO);
    if (!heap_vm) {
        err = -ENOMEM;
        goto error;
    }

    heap_vm->base  = proc->heap_start;
    heap_vm->size  = 0;
    heap_vm->flags = VM_URW;

    if (!(heap_vm->qnode = enqueue(&proc->vm_space.vm_entries, heap_vm))) {
        kfree(heap_vm);
        err = -ENOMEM;
        goto error;
    }

    heap_vm->vm_object = NULL;

    proc->heap_vm  = heap_vm;

    /* the stack's mapping */
    struct vm_entry *stack_vm = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, M_ZERO);
    if (!stack_vm) {
        err = -ENOMEM;
        goto error;
    }

    stack_vm->base  = USER_STACK_BASE;
    stack_vm->size  = USER_STACK_SIZE;
    stack_vm->flags = VM_URW;

    if (!(stack_vm->qnode = enqueue(&proc->vm_space.vm_entries, stack_vm))) {
        kfree(stack_vm);
        err = -ENOMEM;
        goto error;
    }

    stack_vm->vm_object = NULL;

    proc->stack_vm  = stack_vm;

    /* the sigcode page: signal handlers return into it */
    if ((err = arch_sigcode_map(proc)))
        goto error;

    struct thread *thread = (struct thread *) proc->threads.head->value;
    thread->stack_base = USER_STACK_BASE;
    thread->stack_size = USER_STACK_SIZE;

    return 0;

error:
    /* drop whatever was loaded so far */
    vm_space_destroy(&proc->vm_space);
    return err;
}

/**
 * Loads a program into a process, for `execve()`. The file must be a
 * regular file that the process may execute, in a format the kernel
 * knows.
 *
 * @param proc  the process, whose memory the program replaces
 * @param path  the program's path
 * @param ref   not used
 * @return 0, or a negative errno: `-ENOENT`, `-EACCES`, `-ENOEXEC` (no
 *         format knows the file) or the loader's
 */
int binfmt_load(struct proc *proc, const char *path, struct proc **ref)
{
    struct vnode *vnode = NULL;
    int err = 0;

    struct uio uio;
    memset(&uio, 0, sizeof(struct uio));

    if (proc) {
        uio = PROC_UIO(proc);
    }

    if ((err = vfs_lookup(path, &uio, &vnode)))
        return err;

    if (!vnode)
        return -ENOENT;

    /* it must be a regular file that the process may execute */
    if (!S_ISREG(vnode->mode)) {
        err = -EACCES;
        goto done;
    }

    if ((err = vfs_access(vnode, &uio, VFS_X)))
        goto done;

    err = -ENOEXEC;

    for (size_t i = 0; i < NR_BINFMT; ++i) {
        if (!binfmt_list[i].check(vnode)) {
            err = binfmt_fmt_load(proc, path, vnode, &binfmt_list[i], ref);
            break;
        }
    }

done:
    /* drop the lookup's reference: the program's mappings hold their own */
    vfs_close(vnode);
    return err;
}
