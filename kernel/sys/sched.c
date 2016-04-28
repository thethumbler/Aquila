#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>

proc_t *head = NULL;

int kidle = 0;

void kernel_idle()
{
	arch_idle();
}

void spawn_init(proc_t *init)
{
	head = init;
	init->next = NULL;
	arch_switch_process(init);
}

void schedule()
{
	for(;;);
	/*
	void *p = arch_sched(s);

	memcpy(&cur_proc->stat, s, sizeof(stat_t));

	arch_sched_end(p);
	*/
}