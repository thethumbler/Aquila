/*
 * aqtest - minimal freestanding harness for kernel test programs
 *
 * Each test is a static i386 ELF that runs as /init from its own initrd.
 * It talks to the kernel through raw `int $0x80' syscalls (no libc), and
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

#include <aquila/errno.h>
#include <aquila/fcntl.h>
#include <aquila/ioctl.h>
#include <aquila/signal.h>
#include <aquila/stat.h>
#include <aquila/syscall.h>

#define DEV(major, minor)   (((major) << 8) | (minor))

/* an address in kernel space, must never be accepted from user space */
#define KADDR       0xC0100000u

static inline int sc3(int n, u32 a, u32 b, u32 c)
{
    int r;
    asm volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

#define SC(n, a, b, c) sc3(n, (u32)(a), (u32)(b), (u32)(c))

static inline int aq_mount(const char *type, const char *dir, void *data)
{
    struct aquila_mount_args m = {type, dir, 0, data};
    return SC(SYS_mount, &m, 0, 0);
}

/*
 * Entry point: record the stack pointer the kernel handed us (tests check
 * its alignment) and call aq_main() like crt0 would call main().
 */
u32 aq_entry_esp;
void aq_main(void);

asm(".globl _start\n"
    "_start:\n"
    "    mov %esp, aq_entry_esp\n"
    "    call aq_main\n"
    "1:  jmp 1b\n");

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
