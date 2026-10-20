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

int signal_send(int pid, int sig);
int signal_proc_send(struct proc *proc, int signal);
int signal_pgrp_send(struct pgroup *pg, int signal);
int signal_proc_post(struct proc *proc, int signal);
int signal_pgrp_post(struct pgroup *pg, int signal);
int signal_next(struct proc *proc);

/* whether a signal signal_next() would return is pending */
int signal_pending(struct proc *proc);

/* stopping and continuing (job control) */
#define WAIT_STOPPED    1
#define WAIT_CONTINUED  2

int  signal_stops(struct proc *proc, int sig);
void signal_stop(int sig);
void signal_stop_pending(void);

/* signals that can't be blocked */
#define SIG_UNMASKABLE  ((1UL << SIGKILL) | (1UL << SIGSTOP))

#endif /* ! _SYS_SIGNAL_H */
