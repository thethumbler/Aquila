#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#include <core/system.h>

extern void (*syscall_table[])();
extern const size_t syscall_cnt;

#endif /* ! _SYS_SYSCALL_H */
