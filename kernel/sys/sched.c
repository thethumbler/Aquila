#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>

proc_t *head = NULL;
proc_t *cur_proc = NULL;

int kidle = 0;

void kernel_idle()
{
	kidle = 1;
	arch_idle();
}

void spawn_proc(proc_t *proc)	/* Starts process execution */
{
	proc->spawned = 1;
	arch_spawn_proc(proc);
}

void spawn_init(proc_t *init)
{
	init_process(init);
	head = init;
	cur_proc = init;
	init->next = NULL;
	init->state = RUNNABLE;
	arch_sched_init();
	spawn_proc(init);
}

void enqueue_process(proc_t *p)
{
	p->next = head;
	head = p;
}

void dequeue_process(proc_t *p)
{
	proc_t *tmp = head;
	while(tmp)
	{
		if(tmp->next == p)
		{
			tmp->next = p->next;
			if(head == p)
				head = p->next;
			cur_proc = head;
			break;
		}
		tmp = tmp->next;
	}
}

void schedule()	/* Called from arch-specific timer event handler */
{
	if(!cur_proc)
		cur_proc = head;

	if(!cur_proc)	/* How did we even get here? */
		panic("Processes queue is not initialized");

	proc_t *proc = cur_proc->next ? cur_proc->next : head;

	while(proc && proc->state != RUNNABLE)
	{
		if(!proc->next)
			proc = head;
		else 
			proc = proc->next;

		if(proc == cur_proc->next)	/* Couldn't find a RUNNABLE process */
			return;
	}

	if(!proc)
		return;

	kidle = 0;
	
	cur_proc = proc;
	if(proc->spawned)
		arch_switch_process(cur_proc = proc);
	else
		spawn_proc(cur_proc);
}