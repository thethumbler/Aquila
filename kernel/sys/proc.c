/*
 *			Process managment helpers
 *
 *
 *	This file is part of Aquila OS and is released under
 *	the terms of GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
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

int get_pid()
{
	static int pid = 0;
	return ++pid;
}

void init_process(proc_t *proc)
{
	proc->pid = get_pid();

	proc->fds  = kmalloc(FDS_COUNT * sizeof(struct file));
	memset(proc->fds, 0, FDS_COUNT * sizeof(struct file));
}

void kill_process(proc_t *proc)
{
	pmman.unmap((uintptr_t) NULL, (uintptr_t) proc->heap);
	pmman.unmap(USER_STACK_BASE, USER_STACK_BASE);

	//arch_kill_proc(proc);

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

void sleep_on(queue_t *queue)
{
	printk("[%d] %s: Sleeping on queue %x\n", cur_proc->pid, cur_proc->name, queue);
	enqueue(queue, cur_proc);
	arch_sleep();
}

void wakeup_queue(queue_t *queue)
{
	while (queue->count > 0) {
		proc_t *proc = dequeue(queue);
		printk("[%d] %s: Waking up from queue %x\n", proc->pid, proc->name, queue);
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
