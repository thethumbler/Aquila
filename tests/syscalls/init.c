/*
 * System call argument validation and semantics
 *
 * - pointers into kernel space must be rejected with EFAULT
 * - descriptors, signal numbers and sizes must be range checked
 * - signals, wait, threads, pipes, unix sockets, exec of bad ELF files
 */

#include "../lib/aqtest.h"

static volatile int got_sig = 0;
static void handler(int sig) { got_sig = sig; }
static void child_handler(int sig) { SC(S_EXIT, 7, 0, 0); (void) sig; }

struct sigaction_ { u32 handler; u32 mask; int flags; };
struct uthread { u32 stack, entry, uentry, arg, attr; };
struct select_args { int nfds; void *r, *w, *e, *t; };
struct mmap_args { void *addr; u32 len; int prot; int flags; int fd; int off; } __attribute__((packed));
struct sockaddr_un { u32 family; char path[128]; };

static volatile int thread_ran = 0;
static void thr(void *u, void *a) { (void) u; (void) a; thread_ran = 1; SC(S_THREAD_EXIT, 0, 0, 0); for (;;); }
static char tstack[8192] __attribute__((aligned(16)));

static unsigned char bad_elf[52 + 32];

void aq_main(void)
{
    aq_begin("syscalls");

    char buf[512];

    check("read kaddr", SC(S_READ, aq_out, KADDR, 10), -14);
    check("write kaddr", SC(S_WRITE, aq_out, KADDR, 10), -14);
    check("uname kaddr", SC(S_UNAME, KADDR, 0, 0), -14);
    check("getcwd kaddr", SC(S_GETCWD, KADDR, 100, 0), -14);
    check("stat kbuf", SC(S_STAT, "/init", KADDR, 0), -14);
    check("stat kpath", SC(S_STAT, KADDR, buf, 0), -14);
    check("open kpath", SC(S_OPEN, KADDR, 0, 0), -14);
    check("stat ok", SC(S_STAT, "/init", buf, 0), 0);
    check("fstat kbuf", SC(S_FSTAT, aq_out, KADDR, 0), -14);
    check("ioctl TCGETS kaddr", SC(S_IOCTL, aq_out, 0x5401, KADDR), -14);
    check("ioctl TCGETS ok", SC(S_IOCTL, aq_out, 0x5401, buf), 0);
    check("ioctl TIOCGWINSZ kaddr", SC(S_IOCTL, aq_out, 0x5413, KADDR), -14);
    check("pipe kaddr", SC(S_PIPE, KADDR, 0, 0), -14);
    int fds[2];
    check("pipe ok", SC(S_PIPE, fds, 0, 0), 0);
    check("fcntl dup big", SC(S_FCNTL, fds[0], 0, 1000), -22);
    check("fcntl dup 10", SC(S_FCNTL, fds[0], 0, 10), 10);
    check("fcntl dup 10 again", SC(S_FCNTL, fds[0], 0, 10), 11);
    check("close 10", SC(S_CLOSE, 10, 0, 0), 0);
    check("close 10 again", SC(S_CLOSE, 10, 0, 0), -9);
    check("close 11", SC(S_CLOSE, 11, 0, 0), 0);
    check("close 999", SC(S_CLOSE, 999, 0, 0), -9);

    /* pipe EOF: child writes then exits, parent must see data then EOF */
    {
        int p[2];
        char rb[16];
        check("pipe2 ok", SC(S_PIPE, p, 0, 0), 0);
        int pid = SC(S_FORK, 0, 0, 0);
        if (pid == 0) {
            SC(S_CLOSE, p[0], 0, 0);
            SC(S_WRITE, p[1], "hello", 5);
            SC(S_EXIT, 0, 0, 0);
        }
        check("pipe parent close w", SC(S_CLOSE, p[1], 0, 0), 0);
        int total = 0, r;
        while ((r = SC(S_READ, p[0], rb + total, 16 - total)) > 0)
            total += r;
        check("pipe read data", total, 5);
        check("pipe read eof", r, 0);
        int st;
        SC(S_WAITPID, pid, &st, 0);
        check("pipe close r", SC(S_CLOSE, p[0], 0, 0), 0);

        /* EPIPE: write with no readers left */
        check("pipe3 ok", SC(S_PIPE, p, 0, 0), 0);
        check("pipe3 close r", SC(S_CLOSE, p[0], 0, 0), 0);
        check("pipe3 write epipe", SC(S_WRITE, p[1], "x", 1), -32);
        check("pipe3 close w", SC(S_CLOSE, p[1], 0, 0), 0);
    }
    check("read badfd", SC(S_READ, 50, buf, 1), -9);

    /* signals */
    int pid = SC(S_GETPID, 0, 0, 0);
    struct sigaction_ sa = {1 /* SIG_IGN */, 0, 0};
    check("sigaction ign", SC(S_SIGACTION, 25, &sa, 0), 0);
    check("kill self ign", SC(S_KILL, pid, 25, 0), 0);
    check("kill self 0", SC(S_KILL, pid, 0, 0), 0);
    check("kill self 99", SC(S_KILL, pid, 99, 0), -22);
    check("kill nopid", SC(S_KILL, 3999, 15, 0), -3);
    check("kill winch dfl", SC(S_KILL, pid, 24, 0), 0);
    check("sigaction kact", SC(S_SIGACTION, 26, KADDR, 0), -14);
    sa.handler = KADDR;
    check("sigaction khandler", SC(S_SIGACTION, 26, &sa, 0), -14);
    sa.handler = (u32) handler;
    check("sigaction handler", SC(S_SIGACTION, 26, &sa, 0), 0);
    check("kill self handler", SC(S_KILL, pid, 26, 0), 0);
    check("handler ran", got_sig, 26);

    /* waitpid */
    int st = 0;
    int c = SC(S_FORK, 0, 0, 0);
    if (!c) SC(S_EXIT, 3, 0, 0);
    check("waitpid NULL", SC(S_WAITPID, c, 0, 0), c);
    c = SC(S_FORK, 0, 0, 0);
    if (!c) { aq_spin(30000000); SC(S_EXIT, 4, 0, 0); }
    check("waitpid WNOHANG", SC(S_WAITPID, c, 0, 1), 0);
    check("waitpid kstat", SC(S_WAITPID, c, KADDR, 0), -14);
    check("waitpid pgrp", SC(S_WAITPID, -12345, 0, 1), -10);
    check("waitpid st", SC(S_WAITPID, c, &st, 0), c);
    check("status", st, 4 << 8);

    /* kill a zombie */
    c = SC(S_FORK, 0, 0, 0);
    if (!c) SC(S_EXIT, 5, 0, 0);
    aq_spin(20000000);
    check("kill zombie", SC(S_KILL, c, 15, 0), 0);
    check("reap zombie", SC(S_WAITPID, c, &st, 0), c);

    /* async signal to busy child (delivered on context switch) */
    c = SC(S_FORK, 0, 0, 0);
    if (!c) {
        struct sigaction_ s2 = {(u32) child_handler, 0, 0};
        SC(S_SIGACTION, 25, &s2, 0);
        for (;;);
    }
    aq_spin(30000000);
    check("kill busy child", SC(S_KILL, c, 25, 0), 0);
    check("busy child wait", SC(S_WAITPID, c, &st, 0), c);
    check("busy child status", st, 7 << 8);

    /* sbrk / munmap */
    int brk = SC(S_SBRK, 0, 0, 0);
    check("sbrk neg", SC(S_SBRK, -0x10000000, 0, 0), -1);
    check("sbrk huge", SC(S_SBRK, 0xB0000000, 0, 0), -1);
    check("sbrk ok", SC(S_SBRK, 0x1000, 0, 0), brk);
    check("munmap len0", SC(S_MUNMAP, brk & ~0xfff, 0, 0), -22);

    /* select */
    u32 rset[2] = {~0u, ~0u};
    struct select_args sel = {100000, rset, 0, 0, 0};
    /* nfds is clamped to the descriptor table size */
    check("select huge nfds", SC(S_SELECT, &sel, 0, 0) >= 0, 1);
    check("select kargs", SC(S_SELECT, KADDR, 0, 0), -14);

    /* mmap */
    void *mret;
    struct mmap_args ma = {(void *) 0xC0000000, 0x1000, 3, 1 | 2, aq_out, 0};
    check("mmap fixed kernel", SC(S_MMAP, &ma, &mret, 0), -22);
    check("mmap kret", SC(S_MMAP, &ma, KADDR, 0), -14);

    /* threads */
    struct uthread ut = {KADDR, (u32) thr, 0, 0, 0};
    check("thread kstack", SC(S_THREAD_CREATE, &ut, 0, 0), -14);
    ut.stack = (u32) &tstack[8192];
    int tid = SC(S_THREAD_CREATE, &ut, 0, 0);
    check("thread join", SC(S_THREAD_JOIN, tid, 0, 0), tid);
    check("thread ran", thread_ran, 1);

    /* sockets */
    int s = SC(S_SOCKET, 3, 4, 0);
    struct sockaddr_un sun = {3, "/init"};
    check("connect nonsock", SC(S_CONNECT, s, &sun, sizeof(sun)), -111);
    check("connect kaddr", SC(S_CONNECT, s, KADDR, 4), -14);
    check("listen unbound", SC(S_LISTEN, s, 5, 0), -22);
    check("fstat sock", SC(S_FSTAT, s, buf, 0), 0);
    check("lseek sock", SC(S_LSEEK, s, 0, 0), -29);
    check("close sock", SC(S_CLOSE, s, 0, 0), 0);

    /* closing a listener refuses a blocked connect() */
    {
        check("mount tmpfs", aq_mount("tmpfs", "/tmp", 0), 0);
        struct sockaddr_un la = {3, "/tmp/sock"};
        int ls = SC(S_SOCKET, 3, 4, 0);
        check("bind", SC(S_BIND, ls, &la, sizeof(la)), 0);
        check("listen", SC(S_LISTEN, ls, 1, 0), 0);
        int cpid = SC(S_FORK, 0, 0, 0);
        if (cpid == 0) {
            SC(S_CLOSE, ls, 0, 0);  /* drop our inherited copy of the listener */
            int cs = SC(S_SOCKET, 3, 4, 0);
            int r = SC(S_CONNECT, cs, &la, sizeof(la));
            SC(S_EXIT, -r, 0, 0);
        }
        aq_spin(50000000);  /* let the child block in connect() */
        check("close listener", SC(S_CLOSE, ls, 0, 0), 0);
        int st = 0;
        check("refused child wait", SC(S_WAITPID, cpid, &st, 0), cpid);
        check("refused connect", (st >> 8) & 0xff, 111);
        int ls2 = SC(S_SOCKET, 3, 4, 0);
        check("connect after close", SC(S_CONNECT, ls2, &la, sizeof(la)), -111);
        SC(S_CLOSE, ls2, 0, 0);
    }

    /* execve */
    char *bad_argv[] = {"x", (char *) KADDR, 0};
    check("execve kargv", SC(S_EXECVE, "/init", bad_argv, 0), -14);
    check("execve kpath", SC(S_EXECVE, KADDR, 0, 0), -14);
    check("execve noent", SC(S_EXECVE, "/nonexistent", 0, 0), -2);

    /* bad ELF loading into kernel space (/tmp is tmpfs, mounted above) */
    unsigned char *e = bad_elf;
    e[0] = 0x7f; e[1] = 'E'; e[2] = 'L'; e[3] = 'F'; e[4] = 1; e[5] = 1; e[6] = 1;
    *(unsigned short *)(e + 16) = 2; *(unsigned short *)(e + 18) = 3; *(u32 *)(e + 20) = 1;
    *(u32 *)(e + 24) = 0x08048000; *(u32 *)(e + 28) = 52; *(unsigned short *)(e + 40) = 52;
    *(unsigned short *)(e + 42) = 32; *(unsigned short *)(e + 44) = 1;
    u32 *ph = (u32 *)(e + 52);
    ph[0] = 1; ph[1] = 0; ph[2] = 0xBFFFF000; ph[3] = 0xBFFFF000; ph[4] = 0x10; ph[5] = 0x10000; ph[6] = 6; ph[7] = 0x1000;
    int fd = SC(S_OPEN, "/tmp/bad", O_CREAT | O_RDWR, 0777);
    check("write bad elf", SC(S_WRITE, fd, bad_elf, sizeof(bad_elf)), sizeof(bad_elf));
    SC(S_CLOSE, fd, 0, 0);
    c = SC(S_FORK, 0, 0, 0);
    if (!c) { SC(S_EXECVE, "/tmp/bad", 0, 0); SC(S_EXIT, 99, 0, 0); }
    check("bad elf wait", SC(S_WAITPID, c, &st, 0), c);
    check("bad elf status", st, (11 << 8) | 11);

    aq_end();
}
