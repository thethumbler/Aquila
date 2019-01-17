#ifndef _PROC_H
#define _PROC_H

#include <core/system.h>

struct proc;
struct pgroup;
struct session;

#include <fs/vfs.h>
#include <ds/queue.h>
#include <sys/thread.h>
#include <sys/signal.h>

extern struct queue *sessions;
struct session {
    /* Session ID */
    pid_t sid;

    /* Process Groups */
    struct queue *pgps;

    /* Session Leader */
    struct proc *leader;

    /* Session node on sessions queue */
    struct qnode *qnode;
};

extern struct queue *pgroups;
struct pgroup {
    /* Process Group ID */
    pid_t pgid;

    /* Associated Session */
    struct session *session;

    /* Session Queue Node */
    struct qnode *session_node;

    /* Processes */
    struct queue *procs;

    /* Process Group Leader */
    struct proc *leader;

    /* Group node on pgroups queue */
    struct qnode *qnode;
};

struct proc {
    /* Process ID */
    pid_t pid;

    /* Associated Process Group */
    struct pgroup *pgrp;
    struct qnode  *pgrp_node;

    /* Process name - XXX */
    char *name;

    /* Open file descriptors */
    struct file *fds;

    /* Parent process */
    struct proc *parent;

    /* Current Working Directory */
    char *cwd;

    /* File mode creation mask */
    mode_t mask;

    /* User ID */
    uid_t uid;

    /* Groupd ID */
    gid_t gid;

    /* Process initial heap pointer */
    uintptr_t heap_start;

    /* Process current heap pointer */
    uintptr_t heap;

    /* Process entry point */  
    uintptr_t entry;

    /* Virtual memory regions */
    struct queue     vmr;
    struct vmr  *heap_vmr;   /* VMR used as heap */
    struct vmr  *stack_vmr;  /* VMR used as stack */

    /* Process threads */
    struct queue threads;

    /* Threads join wait queue */
    struct queue thread_join;

    /* Number of threads */
    size_t threads_nr;

    /* Recieved Signals Queue */
    struct queue *sig_queue;

    /* Registered signal handlers */
    struct sigaction sigaction[SIG_MAX+1];

    /* Dummy queue for children wait */
    struct queue wait_queue;

    /* Exit status of process */
    int exit;

    /* Arch specific data */
    void *arch; 

    /* Process is running? */
    int running;
};

/* sys/fork.c */
int proc_fork(struct thread *thread, struct proc **fork);

/* sys/execve.c */
int proc_execve(struct thread *thread, const char *fn, char * const argv[], char * const env[]);

/* sys/proc.c */
int  proc_pid_alloc(void);
void proc_pid_free(int pid);
int  proc_new(struct proc **ref);
struct proc *proc_pid_find(pid_t pid);
int session_new(struct proc *proc);
int pgrp_new(struct proc *proc, struct pgroup **ref_pgrp);

void proc_kill(struct proc *proc);
int  proc_reap(struct proc *proc);
int  proc_ptr_validate(struct proc *proc, void *ptr);
int  proc_fd_get(struct proc *proc);
void proc_fd_release(struct proc *proc, int fd);
void proc_dump(struct proc *proc);

int  proc_init(struct proc *proc);

#define PROC_EXIT(info, code) ((((info) & 0xff) << 8) | ((code) & 0xff))
#define PROC_UIO(proc) ((struct uio){.cwd = (proc)->cwd, .uid = (proc)->uid, .gid = (proc)->gid, .mask = (proc)->mask})

extern struct queue *procs;

#endif /* !_PROC_H */
