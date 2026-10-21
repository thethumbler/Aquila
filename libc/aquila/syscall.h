#ifndef _AQUILA_LIBC_SYSCALL_H
#define _AQUILA_LIBC_SYSCALL_H

#include <errno.h>

/* raw system call, returns the result or a negated errno: the number in
 * eax, the arguments in ebx, ecx, edx, esi and edi; on x86-64 in rdi,
 * rsi, rdx, r10 and r8 */
static inline long __syscall(long n, long a, long b, long c)
{
    long ret;

#ifdef __x86_64__
    asm volatile("int $0x80"
            : "=a"(ret)
            : "a"(n), "D"(a), "S"(b), "d"(c)
            : "memory");
#else
    asm volatile("int $0x80"
            : "=a"(ret)
            : "a"(n), "b"(a), "c"(b), "d"(c)
            : "memory");
#endif

    return ret;
}

/* the same with five arguments */
static inline long __syscall5(long n, long a, long b, long c, long d, long e)
{
    long ret;

#ifdef __x86_64__
    register long r10 asm("r10") = d;
    register long r8 asm("r8") = e;

    asm volatile("int $0x80"
            : "=a"(ret)
            : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
            : "memory");
#else
    asm volatile("int $0x80"
            : "=a"(ret)
            : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
            : "memory");
#endif

    return ret;
}

/* convert a negated errno result to the C convention */
static inline long __syscall_ret(long ret)
{
    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

#define SYSCALL(n, a, b, c) \
    __syscall_ret(__syscall((n), (long) (a), (long) (b), (long) (c)))

#define SYSCALL5(n, a, b, c, d, e) \
    __syscall_ret(__syscall5((n), (long) (a), (long) (b), (long) (c), (long) (d), (long) (e)))

#endif /* ! _AQUILA_LIBC_SYSCALL_H */
