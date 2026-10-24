#ifndef _SYS_WAITQ_H
#define _SYS_WAITQ_H

#include <ds/list.h>

struct thread;

/*
 * A wait queue: the threads sleeping until something happens (data to
 * read, room to write, a child to exit, a disk transfer done). A thread
 * sleeps on one (waitq_sleep()), what it waits for wakes them all up
 * when it happens (waitq_wakeup()), and each looks again: sys/waitq.c.
 * The threads are linked through their run_link, which is on the ready
 * list instead while they run: going to sleep or waking up allocates
 * nothing. {0} initializes one.
 */
struct waitq {
    struct list threads;
};

/* the current thread sleeps on `waitq' until woken up; a signal ends the
 * sleep (-ERESTARTSYS), but not an _unint one's */
int  waitq_sleep(struct waitq *waitq);
void waitq_sleep_unint(struct waitq *waitq);

/* every thread on `waitq' wakes up (ready), and every thread polling it;
 * _nopoll: not those (no descriptor is waited on through it) */
void waitq_wakeup(struct waitq *waitq);
void waitq_wakeup_nopoll(struct waitq *waitq);

/* `thread' wakes up, if it sleeps on a wait queue */
void thread_wakeup(struct thread *thread);

/* select() and poll(): the wait queues of the files the current thread
 * waits for, its sleep ended by a wakeup of one (NULL: of any) */
int  poll_register(struct waitq *waitq);
void poll_unregister(struct thread *thread);

#endif /* ! _SYS_WAITQ_H */
