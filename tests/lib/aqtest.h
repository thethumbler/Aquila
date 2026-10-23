/*
 * aqtest - minimal freestanding harness for kernel test programs
 *
 * Each test is a static ELF (i386, x86_64 or aarch64, the kernel's) that
 * runs as /init from its own initrd. It talks to the kernel through raw
 * system calls (`int $0x80', aarch64's `svc #0'; no libc), and
 * reports results on the first serial port, one line per check:
 *
 *      PASS <name> got=<value> want=<value>
 *      FAIL <name> got=<value> want=<value>
 *      aqtest: done, fails=<count>
 *
 * tests/run.sh boots each test in QEMU and parses these lines.
 *
 * A test provides `void aq_main(void)', calls aq_begin() first and
 * aq_end() last.
 */

#ifndef _AQTEST_H
#define _AQTEST_H

#include <stdint.h>

typedef uint32_t u32;
typedef unsigned long uptr;     /* a pointer's size */

#include <aquila/errno.h>
#include <aquila/fcntl.h>
#include <aquila/ioctl.h>
#include <aquila/signal.h>
#include <aquila/stat.h>
#include <aquila/syscall.h>
#include <aquila/time.h>

#define DEV(major, minor)   (((major) << 8) | (minor))

/* an address in kernel space, must never be accepted from user space */
#if defined(__x86_64__)
#define KADDR       0xFFFFFFFF80100000ul
#elif defined(__aarch64__)
#define KADDR       0xFFFF000040080000ul
#else
#define KADDR       0xC0100000u
#endif

/* the number in eax, the arguments in ebx, ecx, edx, esi and edi; on
 * x86_64 in rdi, rsi, rdx, r10 and r8; on aarch64 the number in x8, the
 * arguments in x0 to x4 */
static inline long sc3(long n, uptr a, uptr b, uptr c)
{
    long r;
#if defined(__aarch64__)
    register uptr x8 asm("x8") = n;
    register uptr x0 asm("x0") = a;
    register uptr x1 asm("x1") = b;
    register uptr x2 asm("x2") = c;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    r = x0;
#elif defined(__x86_64__)
    asm volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory");
#else
    asm volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
#endif
    return r;
}

#define SC(n, a, b, c) sc3(n, (uptr)(a), (uptr)(b), (uptr)(c))

static inline long sc5(long n, uptr a, uptr b, uptr c, uptr d, uptr e)
{
    long r;
#if defined(__aarch64__)
    register uptr x8 asm("x8") = n;
    register uptr x0 asm("x0") = a;
    register uptr x1 asm("x1") = b;
    register uptr x2 asm("x2") = c;
    register uptr x3 asm("x3") = d;
    register uptr x4 asm("x4") = e;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
    r = x0;
#elif defined(__x86_64__)
    register uptr r10 asm("r10") = d;
    register uptr r8 asm("r8") = e;
    asm volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8) : "memory");
#else
    asm volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
#endif
    return r;
}

#define SC5(n, a, b, c, d, e) sc5(n, (uptr)(a), (uptr)(b), (uptr)(c), (uptr)(d), (uptr)(e))

static inline int aq_mount(const char *type, const char *dir, void *data)
{
    struct aquila_mount_args m = {type, dir, 0, data};
    return SC(SYS_mount, &m, 0, 0);
}

/*
 * Entry point: record the stack pointer the kernel handed us (tests check
 * its alignment) and call aq_main() like crt0 would call main().
 */
uptr aq_entry_esp;
void aq_main(void);

#if defined(__aarch64__)
/* aarch64: main()'s argc and argv come in x0 and x1 */
long aq_argc;
char **aq_argv;

asm(".globl _start\n"
    "_start:\n"
    "    mov x9, sp\n"
    "    adrp x10, aq_entry_esp\n"
    "    str x9, [x10, :lo12:aq_entry_esp]\n"
    "    adrp x10, aq_argc\n"
    "    str x0, [x10, :lo12:aq_argc]\n"
    "    adrp x10, aq_argv\n"
    "    str x1, [x10, :lo12:aq_argv]\n"
    "    bl aq_main\n"
    "1:  b 1b\n");
#elif defined(__x86_64__)
asm(".globl _start\n"
    "_start:\n"
    "    mov %rsp, aq_entry_esp(%rip)\n"
    "    call aq_main\n"
    "1:  jmp 1b\n");
#else
asm(".globl _start\n"
    "_start:\n"
    "    mov %esp, aq_entry_esp\n"
    "    call aq_main\n"
    "1:  jmp 1b\n");
#endif

static int aq_out = -1;
static int aq_fails = 0;

static int aq_strlen(const char *s)
{
    int n = 0;
    while (s[n]) ++n;
    return n;
}

static void aq_puts(const char *s)
{
    SC(SYS_write, aq_out, s, aq_strlen(s));
}

static void aq_putn(int v)
{
    char b[16];
    int i = 15;
    int neg = v < 0;
    unsigned u = neg ? -(unsigned) v : (unsigned) v;

    b[i] = 0;
    do { b[--i] = '0' + u % 10; u /= 10; } while (u);
    if (neg) b[--i] = '-';

    aq_puts(&b[i]);
}

static void check(const char *name, int got, int want)
{
    aq_puts(got == want ? "PASS " : "FAIL ");
    aq_puts(name);
    aq_puts(" got=");  aq_putn(got);
    aq_puts(" want="); aq_putn(want);
    aq_puts("\n");

    if (got != want)
        ++aq_fails;
}

/* fork, run fn in the child (exit 0x42 if it returns), return wait status */
static int aq_child_status(void (*fn)(void))
{
    int pid = SC(SYS_fork, 0, 0, 0);

    if (pid == 0) {
        fn();
        SC(SYS_exit, 0x42, 0, 0);
        for (;;);
    }

    int status = -1;
    SC(SYS_waitpid, pid, &status, 0);
    return status;
}

/* spin for a while, letting other processes run */
static void aq_spin(int n)
{
    for (volatile int i = 0; i < n; ++i);
}

/* mount devfs and open the first serial port for reporting */
static void aq_begin(const char *name)
{
    static struct { char *dev, *opt; } no_args = {0, 0};

    aq_mount("devfs", "/dev", &no_args);
    SC(SYS_mknod, "/dev/ttyS0", S_IFCHR | 0666, DEV(4, 64));
    aq_out = SC(SYS_open, "/dev/ttyS0", O_RDWR, 0);

    aq_puts("aqtest: ");
    aq_puts(name);
    aq_puts(" start\n");
}

static void __attribute__((noreturn)) aq_end(void)
{
    aq_puts("aqtest: done, fails=");
    aq_putn(aq_fails);
    aq_puts("\n");

    for (;;)
        SC(SYS_getpid, 0, 0, 0);
}

#endif /* ! _AQTEST_H */
