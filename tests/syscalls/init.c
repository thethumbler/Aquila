/*
 * System call argument validation and semantics
 *
 * - pointers into kernel space must be rejected with EFAULT
 * - descriptors, signal numbers and sizes must be range checked
 * - signals, wait, threads, pipes, unix sockets, exec of bad ELF files
 */

#include "../lib/aqtest.h"

#include <aquila/mman.h>
#include <aquila/un.h>

static volatile int got_sig = 0;
static void handler(int sig) { got_sig = sig; }
static void child_handler(int sig) { SC(SYS_exit, 7, 0, 0); (void) sig; }


static volatile int thread_ran = 0;
static void thr(void *u, void *a) { (void) u; (void) a; thread_ran = 1; SC(SYS_thread_exit, 0, 0, 0); for (;;); }
static char tstack[8192] __attribute__((aligned(16)));

static unsigned char bad_elf[52 + 32];

void aq_main(void)
{
    aq_begin("syscalls");

    char buf[512];

    check("read kaddr", SC(SYS_read, aq_out, KADDR, 10), -EFAULT);
    check("write kaddr", SC(SYS_write, aq_out, KADDR, 10), -EFAULT);
    check("uname kaddr", SC(SYS_uname, KADDR, 0, 0), -EFAULT);
    check("getcwd kaddr", SC(SYS_getcwd, KADDR, 100, 0), -EFAULT);
    check("stat kbuf", SC(SYS_stat, "/init", KADDR, 0), -EFAULT);
    check("stat kpath", SC(SYS_stat, KADDR, buf, 0), -EFAULT);
    check("open kpath", SC(SYS_open, KADDR, 0, 0), -EFAULT);
    check("stat ok", SC(SYS_stat, "/init", buf, 0), 0);
    check("fstat kbuf", SC(SYS_fstat, aq_out, KADDR, 0), -EFAULT);
    check("ioctl TCGETS kaddr", SC(SYS_ioctl, aq_out, TCGETS, KADDR), -EFAULT);
    check("ioctl TCGETS ok", SC(SYS_ioctl, aq_out, TCGETS, buf), 0);
    check("ioctl TIOCGWINSZ kaddr", SC(SYS_ioctl, aq_out, TIOCGWINSZ, KADDR), -EFAULT);
    check("pipe kaddr", SC(SYS_pipe, KADDR, 0, 0), -EFAULT);
    int fds[2];
    check("pipe ok", SC(SYS_pipe, fds, 0, 0), 0);
    check("fcntl dup big", SC(SYS_fcntl, fds[0], F_DUPFD, 1000), -EINVAL);
    check("fcntl dup 10", SC(SYS_fcntl, fds[0], F_DUPFD, 10), 10);
    check("fcntl dup 10 again", SC(SYS_fcntl, fds[0], F_DUPFD, 10), 11);
    check("close 10", SC(SYS_close, 10, 0, 0), 0);
    check("close 10 again", SC(SYS_close, 10, 0, 0), -EBADF);
    check("close 11", SC(SYS_close, 11, 0, 0), 0);
    check("close 999", SC(SYS_close, 999, 0, 0), -EBADF);

    /* pipe EOF: child writes then exits, parent must see data then EOF */
    {
        int p[2];
        char rb[16];
        check("pipe2 ok", SC(SYS_pipe, p, 0, 0), 0);
        int pid = SC(SYS_fork, 0, 0, 0);
        if (pid == 0) {
            SC(SYS_close, p[0], 0, 0);
            SC(SYS_write, p[1], "hello", 5);
            SC(SYS_exit, 0, 0, 0);
        }
        check("pipe parent close w", SC(SYS_close, p[1], 0, 0), 0);
        int total = 0, r;
        while ((r = SC(SYS_read, p[0], rb + total, 16 - total)) > 0)
            total += r;
        check("pipe read data", total, 5);
        check("pipe read eof", r, 0);
        int st;
        SC(SYS_waitpid, pid, &st, 0);
        check("pipe close r", SC(SYS_close, p[0], 0, 0), 0);

        /* EPIPE: write with no readers left, SIGPIPE ignored */
        struct aquila_sigaction ign = {1 /* SIG_IGN */, 0, 0}, dfl = {0 /* SIG_DFL */, 0, 0};
        SC(SYS_sigaction, SIGPIPE, &ign, 0);
        check("pipe3 ok", SC(SYS_pipe, p, 0, 0), 0);
        check("pipe3 close r", SC(SYS_close, p[0], 0, 0), 0);
        check("pipe3 write epipe", SC(SYS_write, p[1], "x", 1), -EPIPE);
        SC(SYS_sigaction, SIGPIPE, &dfl, 0);

        /* by default SIGPIPE kills the writer */
        int pc = SC(SYS_fork, 0, 0, 0);
        if (!pc) {
            SC(SYS_write, p[1], "x", 1);
            SC(SYS_exit, 0, 0, 0);
        }
        int pst = 0;
        SC(SYS_waitpid, pc, &pst, 0);
        check("pipe3 SIGPIPE", pst & 0x7f, SIGPIPE);
        check("pipe3 close w", SC(SYS_close, p[1], 0, 0), 0);
    }

    /* a write larger than a pipe holds, with the reader asleep */
    int bp[2];
    SC(SYS_pipe, bp, 0, 0);
    int bc = SC(SYS_fork, 0, 0, 0);
    if (!bc) {
        int total = 0, n;
        SC(SYS_close, bp[1], 0, 0);
        while ((n = SC(SYS_read, bp[0], buf, sizeof(buf))) > 0)
            total += n;
        SC(SYS_exit, total == 3000 ? 0 : 1, 0, 0);
    }
    SC(SYS_close, bp[0], 0, 0);
    aq_spin(10000000);  /* let the reader block first */
    static char big[3000];
    check("big pipe write", SC(SYS_write, bp[1], big, sizeof(big)), 3000);
    SC(SYS_close, bp[1], 0, 0);
    int bst = 0;
    SC(SYS_waitpid, bc, &bst, 0);
    check("reader got it all", bst >> 8, 0);

    check("read badfd", SC(SYS_read, 50, buf, 1), -EBADF);

    /* signals */
    int pid = SC(SYS_getpid, 0, 0, 0);
    struct aquila_sigaction sa = {1 /* SIG_IGN */, 0, 0};
    check("sigaction ign", SC(SYS_sigaction, SIGUSR1, &sa, 0), 0);
    check("kill self ign", SC(SYS_kill, pid, SIGUSR1, 0), 0);
    check("kill self 0", SC(SYS_kill, pid, 0, 0), 0);
    check("kill self 99", SC(SYS_kill, pid, 99, 0), -EINVAL);
    check("kill nopid", SC(SYS_kill, 3999, SIGTERM, 0), -ESRCH);
    check("kill winch dfl", SC(SYS_kill, pid, SIGWINCH, 0), 0);
    check("sigaction kact", SC(SYS_sigaction, SIGUSR2, KADDR, 0), -EFAULT);
    sa.sa_handler = KADDR;
    check("sigaction khandler", SC(SYS_sigaction, SIGUSR2, &sa, 0), -EFAULT);
    sa.sa_handler = (u32) handler;
    check("sigaction handler", SC(SYS_sigaction, SIGUSR2, &sa, 0), 0);
    check("kill self handler", SC(SYS_kill, pid, SIGUSR2, 0), 0);
    check("handler ran", got_sig, SIGUSR2);

    /* waitpid */
    int st = 0;
    int c = SC(SYS_fork, 0, 0, 0);
    if (!c) SC(SYS_exit, 3, 0, 0);
    check("waitpid NULL", SC(SYS_waitpid, c, 0, 0), c);
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) { aq_spin(30000000); SC(SYS_exit, 4, 0, 0); }
    check("waitpid WNOHANG", SC(SYS_waitpid, c, 0, 1), 0);
    check("waitpid kstat", SC(SYS_waitpid, c, KADDR, 0), -EFAULT);
    check("waitpid pgrp", SC(SYS_waitpid, -12345, 0, 1), -ECHILD);
    check("waitpid st", SC(SYS_waitpid, c, &st, 0), c);
    check("status", st, 4 << 8);

    /* kill a zombie */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) SC(SYS_exit, 5, 0, 0);
    aq_spin(20000000);
    check("kill zombie", SC(SYS_kill, c, SIGTERM, 0), 0);
    check("reap zombie", SC(SYS_waitpid, c, &st, 0), c);

    /* async signal to busy child (delivered on context switch) */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        struct aquila_sigaction s2 = {(u32) child_handler, 0, 0};
        SC(SYS_sigaction, SIGUSR1, &s2, 0);
        for (;;);
    }
    aq_spin(30000000);
    check("kill busy child", SC(SYS_kill, c, SIGUSR1, 0), 0);
    check("busy child wait", SC(SYS_waitpid, c, &st, 0), c);
    check("busy child status", st, 7 << 8);

    /* sbrk / munmap */
    int brk = SC(SYS_sbrk, 0, 0, 0);
    check("sbrk neg", SC(SYS_sbrk, -0x10000000, 0, 0), -1);
    check("sbrk huge", SC(SYS_sbrk, 0xB0000000, 0, 0), -1);
    check("sbrk ok", SC(SYS_sbrk, 0x1000, 0, 0), brk);
    check("munmap len0", SC(SYS_munmap, brk & ~0xfff, 0, 0), -EINVAL);

    /* select */
    u32 rset[2] = {~0u, ~0u};
    struct aquila_select_args sel = {100000, rset, 0, 0, 0};
    /* nfds is clamped to the descriptor table size */
    check("select huge nfds", SC(SYS_select, &sel, 0, 0) >= 0, 1);
    check("select kargs", SC(SYS_select, KADDR, 0, 0), -EFAULT);

    /* mmap */
    void *mret;
    struct aquila_mmap_args ma = {(void *) 0xC0000000, 0x1000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE, aq_out, 0};
    check("mmap fixed kernel", SC(SYS_mmap, &ma, &mret, 0), -EINVAL);
    check("mmap kret", SC(SYS_mmap, &ma, KADDR, 0), -EFAULT);

    /* threads */
    struct aquila_thread_args ut = {KADDR, (u32) thr, 0, 0, 0};
    check("thread kstack", SC(SYS_thread_create, &ut, 0, 0), -EFAULT);
    ut.stack = (u32) &tstack[8192];
    int tid = SC(SYS_thread_create, &ut, 0, 0);
    check("thread join", SC(SYS_thread_join, tid, 0, 0), tid);
    check("thread ran", thread_ran, 1);

    /* sockets */
    int s = SC(SYS_socket, AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sun = {AF_UNIX, "/init"};
    check("connect nonsock", SC(SYS_connect, s, &sun, sizeof(sun)), -ECONNREFUSED);
    check("connect kaddr", SC(SYS_connect, s, KADDR, 4), -EFAULT);
    check("listen unbound", SC(SYS_listen, s, 5, 0), -EINVAL);
    check("fstat sock", SC(SYS_fstat, s, buf, 0), 0);
    check("lseek sock", SC(SYS_lseek, s, 0, 0), -ESPIPE);
    check("close sock", SC(SYS_close, s, 0, 0), 0);

    /* closing a listener refuses a blocked connect() */
    {
        check("mount tmpfs", aq_mount("tmpfs", "/tmp", 0), 0);
        struct sockaddr_un la = {AF_UNIX, "/tmp/sock"};
        int ls = SC(SYS_socket, AF_UNIX, SOCK_STREAM, 0);
        check("bind", SC(SYS_bind, ls, &la, sizeof(la)), 0);
        check("listen", SC(SYS_listen, ls, 1, 0), 0);
        int cpid = SC(SYS_fork, 0, 0, 0);
        if (cpid == 0) {
            SC(SYS_close, ls, 0, 0);  /* drop our inherited copy of the listener */
            int cs = SC(SYS_socket, AF_UNIX, SOCK_STREAM, 0);
            int r = SC(SYS_connect, cs, &la, sizeof(la));
            SC(SYS_exit, -r, 0, 0);
        }
        aq_spin(50000000);  /* let the child block in connect() */
        check("close listener", SC(SYS_close, ls, 0, 0), 0);
        int st = 0;
        check("refused child wait", SC(SYS_waitpid, cpid, &st, 0), cpid);
        check("refused connect", (st >> 8) & 0xff, ECONNREFUSED);
        int ls2 = SC(SYS_socket, AF_UNIX, SOCK_STREAM, 0);
        check("connect after close", SC(SYS_connect, ls2, &la, sizeof(la)), -ECONNREFUSED);
        SC(SYS_close, ls2, 0, 0);
    }

    /* execve */
    char *bad_argv[] = {"x", (char *) KADDR, 0};
    check("execve kargv", SC(SYS_execve, "/init", bad_argv, 0), -EFAULT);
    check("execve kpath", SC(SYS_execve, KADDR, 0, 0), -EFAULT);
    check("execve noent", SC(SYS_execve, "/nonexistent", 0, 0), -ENOENT);

    /* bad ELF loading into kernel space (/tmp is tmpfs, mounted above) */
    unsigned char *e = bad_elf;
    e[0] = 0x7f; e[1] = 'E'; e[2] = 'L'; e[3] = 'F'; e[4] = 1; e[5] = 1; e[6] = 1;
    *(unsigned short *)(e + 16) = 2; *(unsigned short *)(e + 18) = 3; *(u32 *)(e + 20) = 1;
    *(u32 *)(e + 24) = 0x08048000; *(u32 *)(e + 28) = 52; *(unsigned short *)(e + 40) = 52;
    *(unsigned short *)(e + 42) = 32; *(unsigned short *)(e + 44) = 1;
    u32 *ph = (u32 *)(e + 52);
    ph[0] = 1; ph[1] = 0; ph[2] = 0xBFFFF000; ph[3] = 0xBFFFF000; ph[4] = 0x10; ph[5] = 0x10000; ph[6] = 6; ph[7] = 0x1000;
    int fd = SC(SYS_open, "/tmp/bad", O_CREAT | O_RDWR, 0777);
    check("write bad elf", SC(SYS_write, fd, bad_elf, sizeof(bad_elf)), sizeof(bad_elf));
    SC(SYS_close, fd, 0, 0);
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) { SC(SYS_execve, "/tmp/bad", 0, 0); SC(SYS_exit, 99, 0, 0); }
    check("bad elf wait", SC(SYS_waitpid, c, &st, 0), c);
    check("bad elf status", st, (SIGSEGV << 8) | SIGSEGV);

    /* user ids: the removed auth syscall, setuid/setgid rules */
    check("auth syscall removed", SC(35, 0, 0, 0), -ENOSYS);

    int fd2 = SC(SYS_open, "/tmp/secret", O_CREAT | O_RDWR, 0600);
    check("create root-only file", fd2 >= 0, 1);
    SC(SYS_close, fd2, 0, 0);

    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        /* exit with the number of the first failed step */
        if (SC(SYS_setgid, 1000, 0, 0) != 0) SC(SYS_exit, 1, 0, 0);
        if (SC(SYS_setuid, 1000, 0, 0) != 0) SC(SYS_exit, 2, 0, 0);
        if (SC(SYS_getuid, 0, 0, 0) != 1000) SC(SYS_exit, 3, 0, 0);
        if (SC(SYS_getgid, 0, 0, 0) != 1000) SC(SYS_exit, 4, 0, 0);
        if (SC(SYS_setuid, 0, 0, 0) != -EPERM) SC(SYS_exit, 5, 0, 0);
        if (SC(SYS_setgid, 0, 0, 0) != -EPERM) SC(SYS_exit, 6, 0, 0);
        if (SC(SYS_setuid, 1000, 0, 0) != 0) SC(SYS_exit, 7, 0, 0);
        if (SC(SYS_open, "/tmp/secret", O_RDONLY, 0) != -EACCES) SC(SYS_exit, 8, 0, 0);
        SC(SYS_exit, 0, 0, 0);
    }
    check("setuid wait", SC(SYS_waitpid, c, &st, 0), c);
    check("setuid/setgid rules (failed step)", st >> 8, 0);
    check("root still root", SC(SYS_getuid, 0, 0, 0), 0);

    /* --- descriptor flags (fcntl) --- */
    fd = SC(SYS_open, "/tmp/flags", O_CREAT | O_WRONLY, 0644);
    check("F_GETFD initially", SC(SYS_fcntl, fd, F_GETFD, 0), 0);
    check("F_SETFD FD_CLOEXEC", SC(SYS_fcntl, fd, F_SETFD, FD_CLOEXEC), 0);
    check("F_GETFD after set", SC(SYS_fcntl, fd, F_GETFD, 0), FD_CLOEXEC);
    /* FD_CLOEXEC == O_WRONLY used to overwrite the open flags */
    check("F_GETFL unchanged", SC(SYS_fcntl, fd, F_GETFL, 0) & O_ACCMODE, O_WRONLY);
    check("still writable", SC(SYS_write, fd, "0123456789", 10), 10);
    int dfd = SC(SYS_fcntl, fd, F_DUPFD_CLOEXEC, 20);
    check("F_DUPFD_CLOEXEC", dfd >= 20, 1);
    check("dup has FD_CLOEXEC", SC(SYS_fcntl, dfd, F_GETFD, 0), FD_CLOEXEC);
    SC(SYS_close, dfd, 0, 0);
    int cfd = SC(SYS_open, "/tmp/flags", O_RDONLY | O_CLOEXEC, 0);
    check("O_CLOEXEC", SC(SYS_fcntl, cfd, F_GETFD, 0), FD_CLOEXEC);

    /* --- ftruncate --- */
    check("ftruncate", SC(SYS_ftruncate, fd, 4, 0), 0);
    char st_buf[128];
    SC(SYS_fstat, fd, st_buf, 0);
    check("size after ftruncate", ((struct aquila_stat *) st_buf)->st_size, 4);
    check("ftruncate read-only fd", SC(SYS_ftruncate, cfd, 0, 0), -EINVAL);
    check("ftruncate negative", SC(SYS_ftruncate, fd, -1, 0), -EINVAL);
    SC(SYS_close, cfd, 0, 0);
    SC(SYS_close, fd, 0, 0);

    /* --- symbolic links (tests/syscalls/rootfs) --- */
    fd = SC(SYS_open, "/lnk/relative", O_RDONLY, 0);
    check("relative symlink", fd >= 0, 1);
    char lbuf[16] = {0};
    check("read through symlink", SC(SYS_read, fd, lbuf, 11), 11);
    SC(SYS_close, fd, 0, 0);
    fd = SC(SYS_open, "/lnk/absolute", O_RDONLY, 0);
    check("absolute symlink", fd >= 0, 1);
    SC(SYS_close, fd, 0, 0);
    check("symlink loop", SC(SYS_open, "/lnk/loop", O_RDONLY, 0), -ELOOP);

    /* --- signal masks --- */
    unsigned long set = 1UL << SIGUSR1, old = 0;
    sa.sa_handler = (u32) handler;
    SC(SYS_sigaction, SIGUSR1, &sa, 0);
    got_sig = 0;
    check("block SIGUSR1", SC(SYS_sigmask, SIG_BLOCK, &set, &old), 0);
    check("old mask empty", old, 0);
    SC(SYS_kill, pid, SIGUSR1, 0);
    check("blocked signal pending", got_sig, 0);
    check("unblock SIGUSR1", SC(SYS_sigmask, SIG_UNBLOCK, &set, 0), 0);
    check("delivered on unblock", got_sig, SIGUSR1);
    unsigned long kset = 1UL << SIGKILL;
    SC(SYS_sigmask, SIG_BLOCK, &kset, 0);
    SC(SYS_sigmask, SIG_BLOCK, 0, &old);
    check("SIGKILL can't be blocked", old & kset, 0);
    check("sigmask bad how", SC(SYS_sigmask, 7, &set, 0), -EINVAL);

    /* sigsuspend: woken by a signal from a child */
    got_sig = 0;
    SC(SYS_sigmask, SIG_BLOCK, &set, 0);
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        aq_spin(20000000);
        SC(SYS_kill, SC(SYS_getppid, 0, 0, 0), SIGUSR1, 0);
        SC(SYS_exit, 0, 0, 0);
    }
    unsigned long none = 0;
    check("sigsuspend -> EINTR", SC(SYS_sigsuspend, &none, 0, 0), -EINTR);
    check("sigsuspend handler ran", got_sig, SIGUSR1);
    SC(SYS_sigmask, SIG_BLOCK, 0, &old);
    check("sigsuspend restores mask", old, set);
    SC(SYS_waitpid, c, &st, 0);
    SC(SYS_sigmask, SIG_SETMASK, &none, 0);

    /* a blocked read interrupted by a signal handler fails with EINTR */
    int ip[2];
    SC(SYS_pipe, ip, 0, 0);
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        aq_spin(20000000);
        SC(SYS_kill, SC(SYS_getppid, 0, 0, 0), SIGUSR1, 0);
        aq_spin(20000000);
        SC(SYS_exit, 0, 0, 0);
    }
    check("blocked read -> EINTR", SC(SYS_read, ip[0], lbuf, 1), -EINTR);
    SC(SYS_waitpid, c, &st, 0);
    SC(SYS_close, ip[0], 0, 0);
    SC(SYS_close, ip[1], 0, 0);

    /* --- processes --- */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c)
        SC(SYS_exit, SC(SYS_getppid, 0, 0, 0) == pid ? 0 : 1, 0, 0);
    SC(SYS_waitpid, c, &st, 0);
    check("getppid", st >> 8, 0);

    /* process groups: a child in its own group, signalled through it */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        struct aquila_sigaction s3 = {(u32) child_handler, 0, 0};
        SC(SYS_sigaction, SIGUSR1, &s3, 0);
        for (;;);
    }
    check("setpgid child", SC(SYS_setpgid, c, c, 0), 0);
    check("setpgid unknown pid", SC(SYS_setpgid, 3999, 3999, 0), -ESRCH);
    /* this test runs as init, the session leader */
    check("setpgid session leader", SC(SYS_setpgid, 0, 0, 0), -EPERM);
    check("join missing group", SC(SYS_setpgid, 0, 4000, 0), -EPERM);
    aq_spin(10000000);
    check("kill group", SC(SYS_kill, -c, SIGUSR1, 0), 0);
    check("group child wait", SC(SYS_waitpid, c, &st, 0), c);
    check("group child status", st, 7 << 8);
    check("kill missing group", SC(SYS_kill, -3999, SIGUSR1, 0), -ESRCH);

    /* --- copy-on-write isolation (CR0.WP) --- */
    static char cow[4096] __attribute__((aligned(4096)));
    /* fault the page in, fork then shares it read-only */
    for (int i = 0; i < 6; ++i)
        ((volatile char *) cow)[i] = "parent"[i];
    int cp[2], go[2];
    char gobyte;
    SC(SYS_pipe, cp, 0, 0);
    SC(SYS_pipe, go, 0, 0);
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        /* look at the page only after the parent wrote to its own copy */
        SC(SYS_read, go[0], &gobyte, 1);
        SC(SYS_exit, ((volatile char *) cow)[0], 0, 0);
    }
    /* fork left the page read-only in the parent, the kernel write must
     * fault and copy it instead of going into the page the child shares */
    SC(SYS_write, cp[1], "PARENT", 6);
    SC(SYS_read, cp[0], cow, 6);
    SC(SYS_write, go[1], "g", 1);
    SC(SYS_waitpid, c, &st, 0);
    check("kernel write didn't leak into child", (st >> 8) & 0xff, 'p');

    aq_end();
}
