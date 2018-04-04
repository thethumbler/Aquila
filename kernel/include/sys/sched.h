#ifndef _SCHED_H
#define _SCHED_H

#include <core/system.h>
#include <sys/proc.h>
#include <ds/queue.h>

extern queue_t *ready_queue;
extern thread_t *cur_thread;

extern int kidle;
void kernel_idle();
void scheduler_init();
void sched_thread_spawn(thread_t *thread);
void sched_init_spawn(proc_t *init);
void schedule();

void sched_thread_ready(thread_t *proc);

#endif /* ! _SCHED_H */
