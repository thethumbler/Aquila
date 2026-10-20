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

    /** Thread stack */
    uintptr_t stack;
    uintptr_t stack_base;
    size_t    stack_size;

    /** Current sleep queue */
    struct queue *sleep_queue;

    /** Executing a system call (it may be sleeping in it) */
    int in_syscall;

    /** Signals are not delivered when the thread is switched to: a system
     *  call that must clean up after being interrupted checks for them and
     *  returns EINTR, they are delivered on the way out */
    int sig_defer;

    /** Handling a page fault (it may be sleeping in it, reading the page
     *  from a disk): signals wait until it is done, the kernel code can't
     *  be left halfway (holding the disk) */
    int in_fault;

    /** In sigsuspend(): the signal mask to restore when a signal is delivered */
    int      sigsuspend;
    sigset_t sigsuspend_mask;
    struct qnode *sleep_node;

    /** In nanosleep(): when to wake up (arch_rtime_ns()) and where to
     *  store the time left if a signal interrupts it */
    uint64_t sleep_until;
    void    *sleep_left;

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
};

int thread_queue_sleep(struct queue *queue);
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
