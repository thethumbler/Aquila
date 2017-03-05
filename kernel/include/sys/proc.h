#ifndef _PROC_H
#define _PROC_H

#include <core/system.h>
#include <fs/vfs.h>
#include <ds/queue.h>

#if ARCH==X86
#include <arch/x86/include/proc.h>
#endif

#define FDS_COUNT	64

typedef enum {
	RUNNABLE,
	ISLEEP,	/* Interruptable SLEEP (I/O) */
	USLEEP,	/* Uninterruptable SLEEP (Waiting for event) */
	ZOMBIE,
	STOPPED,
} state_t;

typedef struct proc proc_t;
struct proc {
	char		*name;
	pid_t 		pid;	/* Process identifier */
	state_t		state;
	struct file *fds;	/* Open file descriptors */
	proc_t 		*parent;
	char 		*cwd;	/* Current Working Directory */
	uintptr_t	heap;	/* Process heap pointer */
	uintptr_t	entry;	/* Process entry point */	

	void		*arch;	/* Arch specific data */
    queue_t     *signals_queue; /* Recieved Signals Queue */
    uintptr_t   signal_handler[22];

	/* Process flags */
	int			spawned : 1;
} __packed;

/* sys/elf.c */
proc_t *load_elf(const char *fn);
proc_t *load_elf_proc(proc_t *proc, const char *fn);

/* sys/fork.c */
proc_t *fork_proc(proc_t *proc);

/* sys/execve.c */
proc_t *execve_proc(proc_t *proc, const char *fn, char * const argv[], char * const env[]);

/* sys/proc.h */
proc_t *get_proc_by_pid(pid_t pid);
int validate_ptr(proc_t *proc, void *ptr);
int get_fd(proc_t *proc);
void release_fd(proc_t *proc, int fd);

int get_pid();
void init_process(proc_t *proc);
void sleep_on(queue_t *queue);
void wakeup_queue(queue_t *queue);

#endif /* !_PROC_H */
