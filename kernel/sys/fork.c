#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/pmap.h>
#include <sys/proc.h>
#include <ds/queue.h>
#include <aquila/errno.h>
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
    fork->sigmask = parent->sigmask;
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
    if ((fork->pid = proc_pid_alloc()) == -1) {
        fork->pid = 0;
        err = -EAGAIN;
        goto error;
    }

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

    if (err)
        goto error;

    /* Return 0 to child */
    arch_syscall_return(fork_thread, 0);
    /* And PID to parent */
    arch_syscall_return(thread, fork->pid);

    if (ref)
        *ref = fork;

    return 0;

error:
    /* the caller returns err to the parent */
    if (fork) {
        /* close copied file descriptors (drops the references we took) */
        if (fork->fds) {
            for (int i = 0; i < FDS_COUNT; ++i) {
                struct file *file = &fork->fds[i];
                if (file->vnode && file->vnode != (void *) -1)
                    vfs_file_close(file);
            }

            kfree(fork->fds);
        }

        vm_space_destroy(&fork->vm_space);
        pmap_decref(fork->vm_space.pmap);

        if (fork->name)
            kfree(fork->name);
        if (fork->cwd)
            kfree(fork->cwd);
        if (fork->sig_queue)
            kfree(fork->sig_queue);

        /* free main thread (it never ran, no arch data) */
        struct thread *_thread = NULL;
        while ((_thread = dequeue(&fork->threads)))
            kfree(_thread);

        if (fork->pgrp_node)
            queue_node_remove(fork->pgrp->procs, fork->pgrp_node);

        if (fork->pid > 0)
            proc_pid_free(fork->pid);

        queue_remove(procs, fork);
        kfree(fork);
    }

    return err;
}
