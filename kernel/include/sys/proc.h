#ifndef _PROC_H
#define _PROC_H

#include <core/system.h>
#include <fs/vfs.h>

#if ARCH==X86
#include <arch/x86/include/proc.h>
#endif

#define FDS_COUNT	64

typedef struct
{
	inode_t *inode;
	size_t  offset;
} file_list_t;

typedef struct proc proc_t;
struct proc
{
	char		*name;
	int 		pid;	/* Process identifier */
	file_list_t *fds;	/* Open file descriptors */
	proc_t 		*parent;
	char 		*cwd;	/* Current Working Directory */
	void		*heap;	/* Process heap pointer */

	void		*arch;	/* Arch specific data */

	proc_t 		*next;	/* Processes queue next pointer */
} __attribute__((packed));

proc_t *load_elf(char *fn);
int get_pid();
int get_fd(proc_t *proc);

#endif /* !_PROC_H */