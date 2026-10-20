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

#endif /* ! _SYS_SIGNAL_H */
