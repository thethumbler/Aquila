#ifndef _SYS_THREAD_H
#define _SYS_THREAD_H

#include <core/system.h>

struct thread;

#include <sys/proc.h>

typedef enum {
    RUNNABLE,
    ISLEEP, /* Interruptable SLEEP (I/O) */
    USLEEP, /* Uninterruptable SLEEP (Waiting for event) */
    ZOMBIE,
} state_t;

/**
 * \ingroup sys
 * \brief thread
 */
struct thread {
    /** Thread ID */
    tid_t tid;

    /** Thread current state */
    state_t state;

    /** Thread owner process */
    struct proc *owner;

    /** A kernel thread's (process 0's): its name, and what it runs */
    const char *name;
    void (*kentry)(void *arg);
    void *karg;

    /** Thread stack */
    uintptr_t stack;
    uintptr_t stack_base;
    size_t    stack_size;

    /** Current sleep queue */
    struct queue *sleep_queue;

    /** The system call it's in (the last it made), to restart it */
    int syscall_nr;

    /** What the thread exited with (thread_exit), for thread_join */
    void *exit_value;

    /** In sigsuspend() or pselect(): the mask they replaced, which the
     *  handler's frame keeps (sigreturn restores it), or which is restored
     *  on the way out (signal_return) */
    int      sig_oldmask_set;
    sigset_t sig_oldmask;
    struct qnode *sleep_node;

    /** Waiting with a deadline (sched_wait): when (arch_rtime_ns()) */
    uint64_t sleep_until;

    /** Address of the last fault, the si_addr of the signal it raised */
    uintptr_t fault_addr;

    /** In the timer's list while waiting with a deadline (sched_wait) */
    struct qnode *timer_node;

    /** Scheduler queue */
    struct queue *sched_queue;
    struct qnode *sched_node;

    /** Arch specific data */
    void *arch;

    /** Thread flags */
    int spawned;

    /** Runs before the others once woken, as soon as the interrupt that
     *  woke it returns (sched_thread_wake): the network's thread */
    int urgent;
};

int  thread_queue_sleep(struct queue *queue);
void thread_queue_sleep_unint(struct queue *queue);
int thread_queue_wakeup(struct queue *queue);
int thread_queue_wakeup_nopoll(struct queue *queue);

/* threads in select() and poll(), woken by every thread_queue_wakeup() */
extern struct queue poll_queue;
void thread_wakeup(struct thread *thread);
int thread_new(struct proc *proc, struct thread **rthread);
int thread_create(struct thread *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg, uintptr_t attr, struct thread **new_thread);
int thread_kill(struct thread *thread);

/* sys/execve.h */
int thread_execve(struct thread *thread, char * const argp[], char * const envp[]);

#endif /* ! _SYS_THREAD_H */
