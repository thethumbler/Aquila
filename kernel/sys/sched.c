#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>

proc_t *head = NULL;
proc_t *cur_proc = NULL;

int kidle = 0;

void kernel_idle()
{
	arch_idle();
}

void spawn_init(proc_t *init)
{
	head = init;
	cur_proc = init;
	init->next = NULL;
	arch_sched_init();
	arch_switch_process(init);
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
			break;
		}
		tmp = tmp->next;
	}
}

void schedule()
{
	arch_sched();

	if(!cur_proc)
		kernel_idle();

	if(!cur_proc->next)
		cur_proc = head;
	else
		cur_proc = cur_proc->next;

	arch_switch_process(cur_proc);
}