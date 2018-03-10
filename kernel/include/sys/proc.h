#ifndef _PROC_H
#define _PROC_H

#include <core/system.h>
#include <fs/vfs.h>
#include <ds/queue.h>

#if ARCH == X86
#include <arch/x86/include/proc.h>
#endif

#define FDS_COUNT	64

typedef enum {
	RUNNABLE,
	ISLEEP,	/* Interruptable SLEEP (I/O) */
	USLEEP,	/* Uninterruptable SLEEP (Waiting for event) */
	ZOMBIE,
} state_t;

typedef struct proc proc_t;
struct proc {
	pid_t 		pid;	/* Process identifier */
	char		*name;  /* Process name */
	state_t		state;  /* Process current state */
	struct file *fds;	/* Open file descriptors */
	proc_t 		*parent;    /* Parent process */
	char 		*cwd;	/* Current Working Directory */
	uintptr_t	heap_start;	/* Process initial heap pointer */
	uintptr_t	heap;	/* Process heap pointer */
	uintptr_t	entry;	/* Process entry point */	

    struct {
        uintptr_t start;    /* Start of process image in memory */
        uintptr_t end;  /* End of process image in memory */
    } __packed image;

	void		*arch;	/* Arch specific data */
    queue_t     *signals_queue; /* Recieved Signals Queue */
    uintptr_t   signal_handler[22];

    queue_t     wait_queue; /* Dummy queue for children wait */
    int         exit_status; /* Exit status of child if zombie */

    uint32_t    mask;
    uint32_t    uid;
    uint32_t    gid;

	/* Process flags */
	int			spawned : 1;
} __packed;

/* sys/fork.c */
proc_t *fork_proc(proc_t *proc);

/* sys/execve.c */
proc_t *execve_proc(proc_t *proc, const char *fn, char * const argv[], char * const env[]);

/* sys/proc.c */
proc_t *new_proc();
proc_t *get_proc_by_pid(pid_t pid);
void kill_proc(proc_t *proc);
void reap_proc(proc_t *proc);
int validate_ptr(proc_t *proc, void *ptr);
int get_fd(proc_t *proc);
void release_fd(proc_t *proc, int fd);

int get_pid();
void init_process(proc_t *proc);
int sleep_on(queue_t *queue);
void wakeup_queue(queue_t *queue);

#endif /* !_PROC_H */
