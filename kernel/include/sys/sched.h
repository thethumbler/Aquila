#ifndef _SYS_SCHED_H
#define _SYS_SCHED_H

#include <core/system.h>
#include <sys/proc.h>
#include <ds/queue.h>

extern struct queue *ready_queue;
extern struct thread *curthread;
#define curproc (curthread->owner)

extern int kidle;
void kernel_idle(void);
void scheduler_init(void);
void sched_thread_spawn(struct thread *thread);
void sched_init_spawn(struct proc *init);
void schedule(void);

void sched_thread_ready(struct thread *thread);

#endif /* ! _SYS_SCHED_H */
