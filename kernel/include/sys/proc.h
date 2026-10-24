#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <core/system.h>

struct proc;
struct pgroup;
struct session;

#include <mm/vm.h>
#include <fs/vfs.h>
#include <ds/queue.h>
#include <sys/waitq.h>
#include <sys/thread.h>
#include <sys/signal.h>
#include <dev/dev.h>

struct tty;

/**
 * session
 */
struct session {
    /** Session ID */
    pid_t sid;

    /** Process Groups */
    struct queue *pgps;

    /** Session Leader */
    struct proc *leader;

    /** Controlling terminal, NULL if none */
    struct tty *ctty;

    /** Session node on sessions queue */
    struct qnode *qnode;
};

/**
 * A file descriptor: the open file it refers to (fs/file.c, shared with
 * the descriptors dup() and fork() copied), and its own flags
 */
struct fd {
    struct file *file;  /* NULL: free; FD_RESERVED: being set up */
    int flags;          /* FD_CLOEXEC */
};

#define FD_RESERVED ((struct file *) -1)

/**
 * process Group
 */
struct pgroup {
    /** Process Group ID */
    pid_t pgid;

    /** Associated Session */
    struct session *session;

    /** Session Queue Node */
    struct qnode *session_node;

    /** Processes */
    struct queue *procs;

    /** Process Group Leader */
    struct proc *leader;

};

/**
 * process
 */
struct proc {
    /** Process ID */
    pid_t pid;

    /** Associated Process Group */
    struct pgroup *pgrp;
    struct qnode  *pgrp_node;

    /** Process name - XXX */
    char *name;

    /** File descriptors (FDS_COUNT), each an open file it refers to */
    struct fd *fds;

    /** Parent process */
    struct proc *parent;

    /** Current Working Directory */
    /** Current directory (a reference), NULL: the root */
    struct vnode *cwd;

    /** File mode creation mask */
    mode_t mask;

    /** stopped by this signal, 0 if not; what waitpid() has to report
     *  (WAIT_*) */
    int stopped;
    int wait_event;

    /** stopped threads sleep here */
    struct waitq stop_queue;

    /** CPU time in user mode and in the kernel (ns), and that of the
     *  children it waited for */
    uint64_t utime, stime;
    uint64_t cutime, cstime;

    /** ITIMER_REAL: SIGALRM when arch_rtime_ns() reaches alarm_at, then
     *  every alarm_interval ns (0: once); on the alarms list while armed */
    uint64_t alarm_at, alarm_interval;
    struct list_node alarm_link;

    /** User ID */
    uid_t uid;

    /** Groupd ID */
    gid_t gid;

    /** Process initial heap pointer */
    uintptr_t heap_start;

    /** Process current heap pointer */
    uintptr_t heap;

    /** Process entry point */  
    uintptr_t entry;

    /** Virtual memory regions */
    struct vm_space vm_space;

    struct vm_entry *heap_vm;
    struct vm_entry *stack_vm;

    /** Process threads */
    struct queue threads;

    /** the last thread id given out (threads leave in any order: their
     * count can't be the next one's) */
    int tid_last;

    /** Threads join wait queue */
    struct waitq thread_join;

    /** Pending signals, (1 << sig): a signal sent again while pending is
     *  delivered once (POSIX leaves it to us, as everyone) */
    sigset_t sig_pending;

    /** Blocked signals, (1 << sig) */
    sigset_t sigmask;

    /** Dummy queue for children wait */
    struct waitq wait_queue;

    /** Registered signal handlers */
    struct aquila_sigaction sigaction[SIG_MAX+1];

    /** Exit status of process */
    int exit;

    /** Process is running? */
    int running;

    /** proc_kill() is done with it, it can be reaped (it isn't done as soon
     * as it stops running: closing its files may sleep) */
    int zombie;
};

/* sys/fork.c */
int proc_fork(struct thread *thread, struct proc **ref);

/* sys/execve.c */
int proc_execve(struct thread *thread, const char *fn, char * const argv[], char * const env[]);

/* sys/proc.c */
pid_t proc_pid_alloc(void);
void  proc_pid_free(int pid);
struct proc *proc_pid_find(pid_t pid);

int proc_new(struct proc **ref);
int session_new(struct proc *proc);
int proc_setpgid(struct proc *proc, pid_t pgid);
void pgrp_release(struct pgroup *pgrp);
int  pgrp_orphaned(struct pgroup *pgrp, struct proc *gone);

void proc_kill(struct proc *proc);
int  proc_threads_end(struct proc *proc, struct thread *keep, int exiting);
void proc_threads_exec(struct proc *proc);
int  proc_reap(struct proc *proc);
int  proc_fd_get(struct proc *proc);
void proc_fd_release(struct proc *proc, int fd);
void proc_fd_install(struct proc *proc, int fd, struct file *file, int flags);
struct file *proc_fd_file(struct proc *proc, int fd);
int  proc_fd_close(struct proc *proc, int fd);
void proc_dump(struct proc *proc);

int  proc_init(struct proc *proc);

#define PROC_EXIT(info, code) ((((info) & 0xff) << 8) | ((code) & 0xff))
#define PROC_UIO(proc) ((struct uio){.cwd = (proc)->cwd, .uid = (proc)->uid, .gid = (proc)->gid, .mask = (proc)->mask})

extern struct queue *procs;

/* init (pid 1): orphans' parent; only it and the kernel's process have none */
extern struct proc *initproc;
extern struct queue *sessions;

#endif /* ! _SYS_PROC_H */
