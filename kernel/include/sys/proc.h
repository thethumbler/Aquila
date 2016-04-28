#ifndef _PROC_H
#define _PROC_H

#include <core/system.h>
#include <fs/vfs.h>

#if ARCH==X86
#include <arch/x86/include/proc.h>
#endif

typedef struct
{
	inode_t *inode;
	size_t  offset;
} proc_file_t;

typedef struct
{
	size_t len;
	proc_file_t *ent;
} file_list_t;

typedef struct proc proc_t;
struct proc
{
	char		*name;
	int 		pid;	/* Process identifier */
	file_list_t fds;	/* Open file descriptors */
	proc_t 		*parent;
	char 		*cwd;	/* Current Working Directory */

	void		*arch;	/* Arch specific data */

	proc_t 		*next;	/* Processes queue next pointer */
} __attribute__((packed));

proc_t *load_elf(char *fn);

#endif /* !_PROC_H */