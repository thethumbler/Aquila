#include <core/system.h>
#include <sys/proc.h>

proc_queue_t *proc_queue;
proc_t *cur_proc;

int kidle = 0;

void kernel_idle()
{
	arch_idle();
}

void schedule(stat_t *s)
{
	void *p = arch_sched(s);

	memcpy(&cur_proc->stat, s, sizeof(stat_t));

	arch_sched_end(p);
}