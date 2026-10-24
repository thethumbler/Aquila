#ifndef _SYS_WAITQ_H
#define _SYS_WAITQ_H

#include <ds/list.h>

struct thread;

/*
 * A wait queue holds the threads sleeping until something happens: data
 * to read, room to write, a child to exit, a disk transfer to finish. A
 * thread sleeps on one with waitq_sleep(); whatever it waits for wakes
 * them all up with waitq_wakeup() when it happens, and each thread looks
 * again. See sys/waitq.c.
 *
 * The threads are linked through their `run_link`, which is on the ready
 * list instead while they can run, so going to sleep or waking up
 * allocates nothing. `{0}` initializes a wait queue.
 */
struct waitq {
    struct list threads;
};

/* Puts the current thread to sleep until woken up, or until a signal. */
int  waitq_sleep(struct waitq *waitq);

/* Puts the current thread to sleep until woken up, whatever signals come. */
void waitq_sleep_unint(struct waitq *waitq);

/* Wakes up every thread on `waitq`, and every thread polling it. */
void waitq_wakeup(struct waitq *waitq);

/* Wakes up every thread on `waitq`, but not the threads polling it. */
void waitq_wakeup_nopoll(struct waitq *waitq);

/* Wakes up `thread`, if it is sleeping on a wait queue. */
void thread_wakeup(struct thread *thread);

/* Registers a wait queue that the current thread, in select() or poll(),
 * waits for (NULL: any). */
int  poll_register(struct waitq *waitq);

/* Removes the registrations of `thread`. */
void poll_unregister(struct thread *thread);

#endif /* ! _SYS_WAITQ_H */
