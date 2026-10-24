#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#include <core/system.h>

/* a system call table entry: the arguments as they come in registers,
 * the result (negative: an errno) */
typedef long (*syscall_t)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

extern const syscall_t syscall_table[];
extern const size_t syscall_cnt;

/* a system call from user mode: sys/syscall.c */
void syscall_dispatch(unsigned long nr, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                      uintptr_t a4, uintptr_t a5);

#endif /* ! _SYS_SYSCALL_H */
