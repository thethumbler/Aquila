/*
 *          Process managment helpers
 *
 *
 *  This file is part of Aquila OS and is released under
 *  the terms of GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */


#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>

#include <mm/mm.h>

#include <sys/proc.h>
#include <sys/elf.h>
#include <sys/sched.h>

#include <fs/vfs.h>

#include <ds/queue.h>

queue_t *procs = NEW_QUEUE; /* All processes queue */

int get_pid()
{
    static int pid = 0;
    return ++pid;
}

proc_t *new_proc()
{
    proc_t *proc = kmalloc(sizeof(proc_t));
    enqueue(procs, proc);   /* Add process to all processes queue */
    return proc;
}

proc_t *get_proc_by_pid(pid_t pid)
{
    forlinked (node, procs->head, node->next) {
        proc_t *proc = node->value;
        if (proc->pid == pid)
            return proc;
    }
    
    return NULL;
}

void init_process(proc_t *proc)
{
    proc->pid = get_pid();

    proc->fds  = kmalloc(FDS_COUNT * sizeof(struct file));
    memset(proc->fds, 0, FDS_COUNT * sizeof(struct file));

    proc->signals_queue = new_queue();  /* Initalize signals queue */
}

void kill_proc(proc_t *proc)
{
    /* Free resources */
    //arch_kill_proc(proc);
    extern proc_t *last_fpu_proc;
    if (last_fpu_proc == proc)
        last_fpu_proc = NULL;

    /* Unmap memory */
    pmman.unmap_full((uintptr_t) NULL, (uintptr_t) proc->heap);
    pmman.unmap_full(USER_STACK_BASE, USER_STACK_SIZE);

    /* Free kernel-space resources */
    kfree(proc->fds);

    while (proc->signals_queue->count)
        dequeue(proc->signals_queue);

    kfree(proc->signals_queue);

    /* Make parent inherit all children */
    forlinked (node, procs->head, node->next) {
        proc_t *_proc = node->value;

        if (_proc->parent == proc)
            _proc->parent = proc->parent;
    }

    proc->state = ZOMBIE;
}

void reap_proc(proc_t *proc)
{
    queue_remove(procs, proc);

    kfree(proc->name);
    kfree(proc);
}

int get_fd(proc_t *proc)
{
    for (int i = 0; i < FDS_COUNT; ++i) {
        if (!proc->fds[i].node) {
            proc->fds[i].node = (void *) -1;    
            return i;
        }
    }

    return -1;
}

void release_fd(proc_t *proc, int fd)
{
    if (fd < FDS_COUNT) {
        proc->fds[fd].node = NULL;
    }
}

int sleep_on(queue_t *queue)
{
    printk("[%d] %s: Sleeping on queue %p\n", cur_proc->pid, cur_proc->name, queue);
    enqueue(queue, cur_proc);
    cur_proc->state = ISLEEP;
    arch_sleep();

    /* Woke up */
    if (cur_proc->state != ISLEEP) {
        /* A signal interrupted the sleep */
        printk("[%d] %s: Sleeping was interrupted by a signal\n", cur_proc->pid, cur_proc->name);
        return -1;
    } else {
        cur_proc->state = RUNNABLE;
        return 0;
    }
}

void wakeup_queue(queue_t *queue)
{
    //printk("wakeup_queue(queue=%p)\n", queue);
    while (queue->count > 0) {
        proc_t *proc = dequeue(queue);
        printk("[%d] %s: Waking up from queue %p\n", proc->pid, proc->name, queue);
        make_ready(proc);
    }
}

int validate_ptr(proc_t *proc, void *ptr)
{
    uintptr_t uptr = (uintptr_t) ptr;
    if (!(uptr >= proc->entry && uptr <= proc->heap))
        return 0;
    return 1;
}
