#ifndef _SYS_SCHED_H
#define _SYS_SCHED_H

#include <core/system.h>
#include <sys/proc.h>
#include <ds/queue.h>

extern struct queue *ready_queue;
extern struct thread *curthread;
#define curproc (curthread->owner)

extern int kidle;

/* processes run, a thread may sleep (before, at boot, only the kernel) */
extern int sched_running;
void kernel_idle(void);
void scheduler_init(void);
void sched_thread_spawn(struct thread *thread);
void sched_init_spawn(struct proc *init);
void schedule(void);
void sched_idle_wakeup(void);

void sched_thread_ready(struct thread *thread);

void sched_timer_tick(uint32_t period, int user);
void sched_sleep(uint64_t until);
int  sched_wait(struct queue *queue, uint64_t until);
void sched_timer_cancel(struct thread *thread);
void sched_sleep_interrupted(struct thread *thread);

#endif /* ! _SYS_SCHED_H */
