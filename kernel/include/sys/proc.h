#ifndef _PROC_H
#define _PROC_H

#include <core/system.h>
#include <fs/vfs.h>
#include <ds/queue.h>

typedef struct proc proc_t;
typedef struct thread thread_t;
typedef struct pgroup pgroup_t;
typedef pid_t tid_t;

#include <sys/signal.h>

typedef struct thread thread_t;

typedef enum {
    RUNNABLE,
    ISLEEP, /* Interruptable SLEEP (I/O) */
    USLEEP, /* Uninterruptable SLEEP (Waiting for event) */
    ZOMBIE,
} state_t;

struct thread {
    tid_t       tid;        /* Thread ID */
    state_t     state;      /* Thread current state */
    proc_t      *owner;     /* Thread owner process */

    struct queue      *sleep_queue; /* Current sleep queue */
    struct queue_node *sleep_node;  /* Sleep queue node */
    struct queue      *sched_queue; /* Scheduler queue */
    struct queue_node *sched_node;  /* Scheduler queue node */

    void        *arch;      /* Arch specific data */

    /* Thread flags */
    int         spawned: 1;
} __packed;

typedef struct session {
    pid_t       sid;        /* Session ID */
    queue_t     *pgps;      /* Process Groups */
    proc_t      *leader;    /* Session Leader */
} session_t;

struct pgroup {
    pid_t       pgid;       /* Process Group ID */
    session_t   *session;   /* Associated Session */

    struct queue_node *session_node;   /* Session Queue Node */

    queue_t     *procs;     /* Processes */
    proc_t      *leader;    /* Process Group Leader */
};

struct proc {
    pid_t       pid;         /* Process ID */
    pgroup_t    *pgrp;       /* Associated Process Group */

    struct queue_node *pgrp_node;   /* Process Group Queue Node */

    char        *name;       /* Process name */
    struct file *fds;        /* Open file descriptors */
    proc_t      *parent;     /* Parent process */
    char        *cwd;        /* Current Working Directory */

    uint32_t    mask;        /* File mode creation mask */
    uint32_t    uid;         /* User ID */
    uint32_t    gid;         /* Groupd ID */

    uintptr_t   heap_start;  /* Process initial heap pointer */
    uintptr_t   heap;        /* Process current heap pointer */
    uintptr_t   entry;       /* Process entry point */  

    queue_t     vmr;         /* Virtual memory regions */
    struct vmr  *heap_vmr;   /* VMR used as heap */
    struct vmr  *stack_vmr;  /* VMR used as stack */

    queue_t     threads;
    queue_t     thread_join; /* Threads join wait queue */
    size_t      threads_nr;  /* Number of threads */

    queue_t     *sig_queue;  /* Recieved Signals Queue */
    struct sigaction sigaction[22];   /* Registered signal handlers */

    queue_t     wait_queue;  /* Dummy queue for children wait */
    int         exit;        /* Exit status of process */

    void        *arch;       /* Arch specific data */

    int         running: 1;  /* Process is running? */
} __packed;

/* sys/fork.c */
int proc_fork(thread_t *thread, proc_t **fork);

/* sys/execve.c */
int proc_execve(thread_t *thread, const char *fn, char * const argv[], char * const env[]);

/* sys/proc.c */
int  proc_pid_alloc();
void proc_pid_free(int pid);
int  proc_new(proc_t **ref);
proc_t *proc_pid_find(pid_t pid);
int session_new(proc_t *proc);
int pgrp_new(proc_t *proc, pgroup_t **ref_pgrp);

void proc_kill(proc_t *proc);
int  proc_reap(proc_t *proc);
int  proc_ptr_validate(proc_t *proc, void *ptr);
int  proc_fd_get(proc_t *proc);
void proc_fd_release(proc_t *proc, int fd);
void proc_dump(proc_t *proc);

int  proc_init(proc_t *proc);

/* sys/thread.c */
int  thread_queue_sleep(queue_t *queue);
void thread_queue_wakeup(queue_t *queue);
int  thread_new(proc_t *proc, thread_t **rthread);
int  thread_create(thread_t *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg, uintptr_t attr, thread_t **new_thread);
int  thread_kill(thread_t *thread);

#define _PROC_EXIT(info, code) ((((info) & 0xff) << 8) | ((code) & 0xff))

#define _PROC_UIO(proc) ((struct uio){.cwd = (proc)->cwd, .uid = (proc)->uid, .gid = (proc)->gid, .mask = (proc)->mask})

extern queue_t *procs;

#endif /* !_PROC_H */
