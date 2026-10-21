#ifndef _AQUILA_SIGNAL_H
#define _AQUILA_SIGNAL_H

/*
 * Signal numbers, following newlib's numbering for i386 so they match
 * what user space sees in <signal.h>.
 */

#define SIGHUP      1   /* hangup */
#define SIGINT      2   /* interrupt */
#define SIGQUIT     3   /* quit */
#define SIGILL      4   /* illegal instruction (not reset when caught) */
#define SIGTRAP     5   /* trace trap (not reset when caught) */
#define SIGIOT      6   /* IOT instruction */
#define SIGABRT     6   /* used by abort, replace SIGIOT in the future */
#define SIGEMT      7   /* EMT instruction */
#define SIGFPE      8   /* floating point exception */
#define SIGKILL     9   /* kill (cannot be caught or ignored) */
#define SIGBUS      10  /* bus error */
#define SIGSEGV     11  /* segmentation violation */
#define SIGSYS      12  /* bad argument to system call */
#define SIGPIPE     13  /* write on a pipe with no one to read it */
#define SIGALRM     14  /* alarm clock */
#define SIGTERM     15  /* software termination signal from kill */
#define SIGURG      16  /* urgent condition on IO channel */
#define SIGSTOP     17  /* sendable stop signal not from tty */
#define SIGTSTP     18  /* stop signal from tty */
#define SIGCONT     19  /* continue a stopped process */
#define SIGCHLD     20  /* to parent on child stop or exit */
#define SIGCLD      20  /* System V name for SIGCHLD */
#define SIGTTIN     21  /* to readers pgrp upon background tty read */
#define SIGTTOU     22  /* like TTIN for output if (tp->t_local&LTOSTOP) */
#define SIGIO       23  /* input/output possible signal */
#define SIGPOLL     SIGIO /* System V name for SIGIO */
#define SIGXCPU     24  /* exceeded CPU time limit */
#define SIGXFSZ     25  /* exceeded file size limit */
#define SIGVTALRM   26  /* virtual time alarm */
#define SIGPROF     27  /* profiling time alarm */
#define SIGWINCH    28  /* window changed */
#define SIGLOST     29  /* resource lost (eg, record-lock lost) */
#define SIGUSR1     30  /* user defined signal 1 */
#define SIGUSR2     31  /* user defined signal 2 */

#define NSIG        32  /* signal 0 implied */

/* signal sets are bit masks, signal n is bit (1 << n) */
#define SIG_SETMASK 0   /* set mask with sigprocmask() */
#define SIG_BLOCK   1   /* set of signals to block */
#define SIG_UNBLOCK 2   /* set of signals to unblock */

/*
 * What a signal handler gets as its second argument (the C library's
 * siginfo_t), the third (ucontext) is NULL. si_addr is the faulting
 * address for SIGSEGV, SIGBUS, SIGILL and SIGFPE.
 *
 * A handler returns into a page the kernel maps in every process (below
 * the stack), which makes the sigreturn system call: nothing in user
 * space has to know about it.
 */
struct aquila_siginfo {
    int           si_signo;
    int           si_code;      /* 0 */
    int           si_value;
    unsigned long si_addr;
};

/* sigaction(2) flags */
#ifndef SA_NOCLDSTOP
#define SA_NOCLDSTOP 0x1    /* no SIGCHLD when a child stops or continues */
#define SA_SIGINFO   0x2    /* every handler gets the siginfo anyway */
#define SA_NOCLDWAIT 0x4    /* children don't stay zombies (SIGCHLD) */
#define SA_RESTART   0x8    /* a call the signal interrupts is restarted */
#define SA_NODEFER   0x10   /* not blocked while its handler runs */
#define SA_RESETHAND 0x20   /* the handler goes (SIG_DFL) once it's run */
#endif

/* sigaction(2) argument, same layout as newlib's struct sigaction */
struct aquila_sigaction {
    unsigned long sa_handler;
    unsigned long sa_mask;
    int           sa_flags;
};

#endif /* ! _AQUILA_SIGNAL_H */
