#ifndef _PROC_H
#define _PROC_H

#include <core/system.h>
#include <fs/vfs.h>

typedef struct 
{
	uint32_t edi, esi, ebp, ebx, ecx, edx, eax,
		eip, cs, eflags, esp, ss;
} __attribute__((packed)) stat_t;

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
	int 		pid;
	file_list_t fds;
	proc_t 		*parent;
	stat_t		stat;
	char 		*cwd;

	proc_t 		*next;
} __attribute__((packed));

proc_t *load_elf(char *fn);

#endif /* !_PROC_H */