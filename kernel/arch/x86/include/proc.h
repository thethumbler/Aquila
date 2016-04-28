#ifndef _X86_PROC_H
#define _X86_PROC_H

#define USER_STACK	(0xC0000000UL)
#define USER_STACK_SIZE	(8192)	/* 8KiB */
#define USER_STACK_BASE (USER_STACK - USER_STACK_SIZE)

#endif /* ! _X86_PROC_H */