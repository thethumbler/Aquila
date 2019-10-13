#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <sys/proc.h>
#include <ds/queue.h>
#include <bits/errno.h>
#include <net/socket.h>

MALLOC_DECLARE(M_FDS);

static int copy_fds(struct proc *parent, struct proc *fork)
{
    /* copy open files descriptors */
    fork->fds = kmalloc(FDS_COUNT * sizeof(struct file), &M_FDS, 0);

    if (!fork->fds)
        return -ENOMEM;

    memcpy(fork->fds, parent->fds, FDS_COUNT * sizeof(struct file));

    for (int i = 0; i < FDS_COUNT; ++i) {
        struct file *file = &fork->fds[i];
        if (file->vnode && (file->vnode != (void *) -1)) {
            if (file->flags & FILE_SOCKET)
                file->socket->ref++;
            else
                file->vnode->ref++;
        }
    }

    return 0;
}

static int fork_proc_copy(struct proc *parent, struct proc *fork)
{
    fork->pgrp = parent->pgrp;
    fork->pgrp_node = enqueue(fork->pgrp->procs, fork);

    fork->mask = parent->mask;
    fork->uid  = parent->uid;
    fork->gid  = parent->gid;

    fork->heap_start = parent->heap_start;
    fork->heap  = parent->heap;
    fork->entry = parent->entry;

    memcpy(fork->sigaction, parent->sigaction, sizeof(parent->sigaction));

    return 0;
}

int proc_fork(struct thread *thread, struct proc **ref)
{
    int err = 0;
    struct proc *fork = NULL;
    struct thread *fork_thread = NULL;

    /* Create new process for fork */
    if ((err = proc_new(&fork)))
        goto error;

    /* New process main thread */
    fork_thread = (struct thread *) fork->threads.head->value;

    /* Parent process */
    struct proc *proc = thread->owner;

    /* Copy process structure */
    if ((err = fork_proc_copy(proc, fork)))
        goto error;

    /* Copy process name */
    if (!(fork->name = strdup(proc->name))) {
        err = -ENOMEM;
        goto error;
    }

    /* Allocate a new PID */
    fork->pid = proc_pid_alloc();

    /* Set fork parent */
    fork->parent = proc;

    /* Mark the new thread as spawned
     * fork continues execution from a spawned thread */
    fork_thread->spawned = 1;

    /* Copy current working directory */
    if (!(fork->cwd = strdup(proc->cwd))) {
        err = -ENOMEM;
        goto error;
    }

    /* Allocate new signals queue */
    if (!(fork->sig_queue = queue_new())) {
        err = -ENOMEM;
        goto error;
    }

    /* copy file descriptors */
    if ((err = copy_fds(proc, fork)))
        goto error;

    /* copy virtual memory space */
    if ((err = vm_space_fork(&proc->vm_space, &fork->vm_space)))
        goto error;

    /* Fix heap & stack entry pointers -- XXX yes, we are doing this */
    struct qnode *pvm_node = proc->vm_space.vm_entries.head;
    struct qnode *fvm_node = fork->vm_space.vm_entries.head;

    while (pvm_node) {
        struct vm_entry *pvm_entry = pvm_node->value;
        struct vm_entry *fvm_entry = fvm_node->value;

        if (pvm_entry == proc->heap_vm)
            fork->heap_vm = fvm_entry;

        if (pvm_entry == proc->stack_vm)
            fork->stack_vm = fvm_entry;

        pvm_node = pvm_node->next;
        fvm_node = fvm_node->next;
    }

    /* Call arch specific fork handler */
    err = arch_proc_fork(thread, fork);

    if (!err) {
        /* Return 0 to child */
        arch_syscall_return(fork_thread, 0);
        /* And PID to parent */
        arch_syscall_return(thread, fork->pid);
    } else {
        /* Return error to parent */
        arch_syscall_return(thread, err);
        goto error;
    }

    if (ref)
        *ref = fork;

    return 0;

error:
    if (fork) {
        if (fork->name)
            kfree(fork->name);
        if (fork->cwd)
            kfree(fork->cwd);
        if (fork->sig_queue)
            kfree(fork->sig_queue);

        /* TODO free VMRs */

        kfree(fork);
    }

    return err;
}
