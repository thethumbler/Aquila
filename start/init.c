/*
 * start/init.c - the first program to read, and to change
 *
 * `make run-min` boots the kernel with this program as /init, the first
 * process, and nothing else: no C library, no shell, no toolchain to
 * build. It is a tiny shell. Each of its commands is one or two system
 * calls, so you can follow every command from here into the kernel:
 *
 *      help                the commands
 *      echo <words>        write(2) the words back
 *      uname               uname(2): what the kernel says it is
 *      ls [dir]            open(2) a directory, readdir(2) its entries
 *      cat <file>          open(2), read(2) and write(2)
 *      mem                 cat /proc/meminfo (procfs: fs/procfs/)
 *      fork                fork(2) a child, waitpid(2) for its exit status
 *      poweroff            reboot(2), to power off
 *
 * A system call is a number and up to three arguments here, passed in
 * registers, then a trap into the kernel: `int $0x80' on x86, `svc #0'
 * on aarch64. The kernel looks the number up in syscall_table[]
 * (kernel/sys/syscall.c) and calls its sys_*() function. The numbers are
 * in include/uapi/aquila/syscall.h. Error results are negative errno
 * values (include/uapi/aquila/errno.h).
 *
 * Try adding a command: a `cd' with SYS_chdir, say, or a `kill'.
 */

#include <stddef.h>
#include <stdint.h>

#include <aquila/syscall.h>
#include <aquila/fcntl.h>
#include <aquila/stat.h>
#include <aquila/dirent.h>
#include <aquila/utsname.h>
#include <aquila/reboot.h>

/*
 * The system call itself: the number in eax (x86) or x8 (aarch64), the
 * arguments in the registers the kernel expects, the result back in eax
 * or x0. See docs/kernel/syscalls.md.
 */
static long syscall3(long n, long a, long b, long c)
{
    long r;
#if defined(__aarch64__)
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a;
    register long x1 asm("x1") = b;
    register long x2 asm("x2") = c;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    r = x0;
#elif defined(__x86_64__)
    asm volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory");
#else
    asm volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
#endif
    return r;
}

#define SC(n, a, b, c)  syscall3(n, (long) (a), (long) (b), (long) (c))

/* The terminal we talk to: the first serial port, /dev/ttyS0. */
static int tty = -1;

static size_t length(const char *s)
{
    size_t n = 0;

    while (s[n])
        n++;

    return n;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }

    return *a == *b;
}

static void put(const char *s)
{
    SC(SYS_write, tty, s, length(s));
}

static void put_number(long v)
{
    char buf[24];
    int i = sizeof(buf) - 1;
    unsigned long u = v < 0 ? -(unsigned long) v : (unsigned long) v;

    buf[i] = 0;

    do {
        buf[--i] = '0' + u % 10;
        u /= 10;
    } while (u);

    if (v < 0)
        buf[--i] = '-';

    put(&buf[i]);
}

/* A failed call: what it was, and the error number the kernel returned. */
static void put_error(const char *what, long err)
{
    put(what);
    put(": error ");
    put_number(-err);
    put("\n");
}

static void cmd_help(void)
{
    put("commands: help, echo <words>, uname, ls [dir], cat <file>, mem, fork, poweroff\n");
}

static void cmd_echo(char **argv, int argc)
{
    for (int i = 1; i < argc; i++) {
        put(argv[i]);
        put(i + 1 < argc ? " " : "");
    }

    put("\n");
}

static void cmd_uname(void)
{
    struct utsname u;
    long err = SC(SYS_uname, &u, 0, 0);

    if (err < 0) {
        put_error("uname", err);
        return;
    }

    put(u.sysname); put(" ");
    put(u.release); put(" ");
    put(u.version); put(" ");
    put(u.machine); put("\n");
}

static void cmd_ls(const char *dir)
{
    long fd = SC(SYS_open, dir, O_RDONLY, 0);

    if (fd < 0) {
        put_error(dir, fd);
        return;
    }

    /* readdir(2) gives one entry each call, 0 at the end */
    struct dirent d;

    while (SC(SYS_readdir, fd, &d, 0) > 0) {
        put(d.d_name);
        put("  ");
    }

    put("\n");
    SC(SYS_close, fd, 0, 0);
}

static void cmd_cat(const char *path)
{
    long fd = SC(SYS_open, path, O_RDONLY, 0);

    if (fd < 0) {
        put_error(path, fd);
        return;
    }

    char buf[512];
    long n;

    while ((n = SC(SYS_read, fd, buf, sizeof(buf))) > 0)
        SC(SYS_write, tty, buf, n);

    SC(SYS_close, fd, 0, 0);
}

/*
 * fork(2): two processes return from the same call. The child gets 0 and
 * exits with status 7; the parent gets the child's process ID and waits
 * for it. kernel/sys/fork.c copies the process (its memory copy on
 * write: docs/kernel/memory.md).
 */
static void cmd_fork(void)
{
    long pid = SC(SYS_fork, 0, 0, 0);

    if (pid < 0) {
        put_error("fork", pid);
        return;
    }

    if (pid == 0) {
        put("child: I am process ");
        put_number(SC(SYS_getpid, 0, 0, 0));
        put(", exiting with 7\n");
        SC(SYS_exit, 7, 0, 0);
    }

    int status = 0;
    SC(SYS_waitpid, pid, &status, 0);

    put("parent: child ");
    put_number(pid);
    put(" exited with ");
    put_number((status >> 8) & 0xFF);
    put("\n");
}

/* Split a line into words, in place; returns how many. */
static int split(char *line, char **argv, int max)
{
    int argc = 0;

    while (*line && argc < max) {
        while (*line == ' ' || *line == '\t')
            *line++ = 0;

        if (!*line)
            break;

        argv[argc++] = line;

        while (*line && *line != ' ' && *line != '\t')
            line++;
    }

    return argc;
}

static void run(char *line)
{
    char *argv[16];
    int argc = split(line, argv, 16);

    if (!argc)
        return;

    if (same(argv[0], "help"))
        cmd_help();
    else if (same(argv[0], "echo"))
        cmd_echo(argv, argc);
    else if (same(argv[0], "uname"))
        cmd_uname();
    else if (same(argv[0], "ls"))
        cmd_ls(argc > 1 ? argv[1] : "/");
    else if (same(argv[0], "cat") && argc > 1)
        cmd_cat(argv[1]);
    else if (same(argv[0], "mem"))
        cmd_cat("/proc/meminfo");
    else if (same(argv[0], "fork"))
        cmd_fork();
    else if (same(argv[0], "poweroff"))
        SC(SYS_reboot, RB_POWER_OFF, 0, 0);
    else {
        put(argv[0]);
        put(": no such command (try help)\n");
    }
}

/* SYS_mount takes its arguments in a structure (include/uapi/aquila/syscall.h). */
static long mount(const char *type, const char *dir)
{
    struct aquila_mount_args args = {type, dir, 0, NULL};
    return SC(SYS_mount, &args, 0, 0);
}

void main(void)
{
    /*
     * The RAM disk has empty /dev and /proc. Device files come from devfs
     * (fs/devfs/), and /dev/ttyS0 is made here: character device 4, 64
     * (the first UART, dev/tty/uart/uart.c). The terminal does the line
     * editing: read(2) returns a whole line once Enter is pressed
     * (dev/tty/generic.c, "canonical mode").
     */
    mount("devfs", "/dev");
    mount("procfs", "/proc");
    SC(SYS_mknod, "/dev/ttyS0", S_IFCHR | 0600, (4 << 8) | 64);
    tty = SC(SYS_open, "/dev/ttyS0", O_RDWR, 0);

    put("\nHello from user mode: /init, process ");
    put_number(SC(SYS_getpid, 0, 0, 0));
    put(". Type help.\n");

    char line[256];

    /* init must never exit: the kernel would have no process to adopt orphans */
    for (;;) {
        put("> ");

        long n = SC(SYS_read, tty, line, sizeof(line) - 1);

        if (n <= 0)
            continue;

        line[n] = 0;

        if (line[n - 1] == '\n')
            line[n - 1] = 0;

        run(line);
    }
}

/*
 * Where the kernel starts the process (the ELF entry point). With no C
 * library there's no crt0 to call main() for us, so these few
 * instructions do: a call, which leaves the stack as C code expects it.
 */
#if defined(__aarch64__)
asm(".globl _start\n_start:\n    bl main\n1:  b 1b\n");
#else
asm(".globl _start\n_start:\n    call main\n1:  jmp 1b\n");
#endif
