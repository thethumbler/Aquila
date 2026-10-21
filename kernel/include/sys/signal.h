#ifndef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H

#include <core/system.h>

struct aquila_sigaction;

#include <sys/proc.h>

/* Signal numbers */
#include <aquila/signal.h>

#define SIG_MAX (NSIG - 1)

#define SIGACT_ABORT        1
#define SIGACT_TERMINATE    2
#define SIGACT_IGNORE       3
#define SIGACT_STOP         4
#define SIGACT_CONTINUE     5

/* libc bindings */
#define SIG_DFL ((uintptr_t) 0)  /* Default action */
#define SIG_IGN ((uintptr_t) 1)  /* Ignore action */
#define SIG_ERR ((uintptr_t) -1) /* Error return */

extern int sig_default_action[SIG_MAX+1];

/*
 * Signals are made pending (a bit each) and the process's sleeping
 * threads woken; they are delivered on the way back to user mode only
 * (signal_take(), from the architecture's return path), never from the
 * middle of the kernel: a sleep they interrupt returns ERESTARTSYS, and
 * the system call unwinds.
 */
int signal_send(int pid, int sig);
int signal_proc_send(struct proc *proc, int signal);
int signal_pgrp_send(struct pgroup *pg, int signal);

/* sigaction() changed sig's action (an ignored one is discarded) */
void signal_action_set(struct proc *proc, int sig);

/* a fault's (SIGSEGV, SIGILL, ...): delivered even if blocked or ignored,
 * by its default action then (the instruction would fault again) */
void signal_force(struct proc *proc, int signal);

/* a signal is pending that the thread's process doesn't block: its
 * interruptible sleeps end */
int signal_interrupts(struct thread *thread);

/* on the way back to user mode: stops taken, default actions done
 * (terminating doesn't return); the signal whose handler is to run, 0 */
int signal_take(void);

/* the current process ends, by `sig' (from where it can't go on) */
void signal_exit(int sig) __attribute__((noreturn));

/* stopping and continuing (job control) */
#define WAIT_STOPPED    1
#define WAIT_CONTINUED  2

int  signal_stops(struct proc *proc, int sig);
void signal_stop(int sig);

/*
 * A system call interrupted by a signal (a sleep returned it): restarted
 * on the way out if no handler runs, or if it has SA_RESTART
 * (ERESTARTSYS); restarted only if no handler runs (ERESTARTNOHAND: the
 * waits POSIX says fail with EINTR whatever SA_RESTART says). User space
 * never sees them, it gets EINTR.
 */
#define ERESTARTSYS     512
#define ERESTARTNOHAND  514

/* signals that can't be blocked */
#define SIG_UNMASKABLE  ((1UL << SIGKILL) | (1UL << SIGSTOP))

#endif /* ! _SYS_SIGNAL_H */
