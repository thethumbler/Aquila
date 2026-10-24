#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#include <core/system.h>

/* A system call table entry: it takes the arguments as they came in
 * registers, and returns the result (negative: an errno). */
typedef long (*syscall_t)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

/* The table, and how many entries it has (sys/syscall.c). */
extern const syscall_t syscall_table[];
extern const size_t syscall_cnt;

/* Runs a system call made from user mode. */
void syscall_dispatch(unsigned long nr, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                      uintptr_t a4, uintptr_t a5);

#endif /* ! _SYS_SYSCALL_H */
