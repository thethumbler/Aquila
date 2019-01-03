#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <core/system.h>

extern void (*syscall_table[])();
extern const size_t syscall_cnt;

#endif /* ! _SYSCALL_H */
