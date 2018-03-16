#ifndef _PROC_H
#define _PROC_H

#include <core/system.h>
#include <fs/vfs.h>
#include <ds/queue.h>

typedef struct thread thread_t;

typedef enum {
	RUNNABLE,
	ISLEEP,	/* Interruptable SLEEP (I/O) */
	USLEEP,	/* Uninterruptable SLEEP (Waiting for event) */
	ZOMBIE,
} state_t;

typedef struct proc proc_t;
typedef struct thread thread_t;
typedef pid_t tid_t;

struct thread {
	tid_t 		tid;	    /* Thread ID */
	state_t		state;      /* Thread current state */
	proc_t 		*owner;     /* Thread owner process */

    struct queue      *sleep_queue; /* Current sleep queue */
    struct queue_node *sleep_node;  /* Sleep queue node */
    struct queue      *sched_queue; /* Scheduler queue */
    struct queue_node *sched_node;  /* Scheduler queue node */

	void		*arch;	    /* Arch specific data */

	/* Thread flags */
	int			spawned: 1;
} __packed;

struct proc {
	pid_t 		pid;	     /* Process ID */
	char		*name;       /* Process name */
	struct file *fds;	     /* Open file descriptors */
	proc_t 		*parent;     /* Parent process */
	char 		*cwd;	     /* Current Working Directory */

    uint32_t    mask;        /* File mode creation mask */
    uint32_t    uid;         /* User ID */
    uint32_t    gid;         /* Groupd ID */

	uintptr_t	heap_start;	 /* Process initial heap pointer */
	uintptr_t	heap;	     /* Process current heap pointer */
	uintptr_t	entry;	     /* Process entry point */	

    //struct vmm  *vmm;       /* Virtual memory sgements */

    queue_t     threads;
    queue_t     thread_join; /* Threads join wait queue */
    size_t      threads_nr;  /* Number of threads */

    queue_t     *sig_queue;  /* Recieved Signals Queue */
    uintptr_t   sig[22];     /* Registered signal handlers */

    queue_t     wait_queue;  /* Dummy queue for children wait */
    int         exit;        /* Exit status of process */

	void		*arch;	     /* Arch specific data */

    int         running: 1;  /* Process is running? */
} __packed;

/* sys/fork.c */
int proc_fork(thread_t *thread, proc_t **fork);

/* sys/execve.c */
int proc_execve(thread_t *thread, const char *fn, char * const argv[], char * const env[]);

/* sys/proc.c */
proc_t *proc_new(void);
proc_t *proc_pid_find(pid_t pid);

void proc_kill(proc_t *proc);
int  proc_reap(proc_t *proc);
int  proc_ptr_validate(proc_t *proc, void *ptr);
int  proc_fd_get(proc_t *proc);
void proc_fd_release(proc_t *proc, int fd);

int  proc_pid_get();
int  proc_init(proc_t *proc);

/* sys/thread.c */
int  thread_queue_sleep(queue_t *queue);
void thread_queue_wakeup(queue_t *queue);
int  thread_new(proc_t *proc, thread_t **rthread);
int  thread_create(thread_t *thread, uintptr_t stack, uintptr_t entry, uintptr_t arg, int attr, thread_t **new_thread);
int  thread_kill(thread_t *thread);

#endif /* !_PROC_H */
