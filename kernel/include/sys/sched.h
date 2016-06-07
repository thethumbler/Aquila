#ifndef _SCHED_H
#define _SCHED_H

#include <core/system.h>
#include <sys/proc.h>
#include <ds/queue.h>

extern queue_t *ready_queue;
extern proc_t *cur_proc;

extern int kidle;
void kernel_idle();
void scheduler_init();
void spawn_proc(proc_t *proc);
void spawn_init(proc_t *init);
void schedule();
void make_ready(proc_t *proc);

#endif /* ! _SCHED_H */