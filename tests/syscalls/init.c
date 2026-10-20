/*
 * System call argument validation and semantics
 *
 * - pointers into kernel space must be rejected with EFAULT
 * - descriptors, signal numbers and sizes must be range checked
 * - signals, wait, threads, pipes, unix sockets, exec of bad ELF files
 * - files: the *at() calls, links, rename, permissions and attributes
 * - nanosleep and clocks
 * - FIFOs
 * - sessions and controlling terminals, on a pty
 * - stopping and continuing processes, the terminal line discipline
 * - select, poll and terminal read timeouts (VTIME)
 * - flock
 * - anonymous memory (mmap)
 * - virtual terminals: KD* and VT_* ioctls, switching, VT_PROCESS
 */

#include "../lib/aqtest.h"

#include <aquila/mman.h>
#include <aquila/un.h>
#include <aquila/termios.h>
#include <aquila/wait.h>
#include <aquila/poll.h>
#include <aquila/reboot.h>
#include <aquila/kd.h>
#include <aquila/vt.h>

static volatile int got_sig = 0;
static void handler(int sig) { got_sig = sig; }

/* VT_PROCESS: the signals asking to release the terminal, and telling it
 * is back */
static volatile int vt_rel = 0, vt_acq = 0;
static void vt_handler(int sig) { if (sig == SIGUSR1) vt_rel++; else vt_acq++; }

/* "/dev/tty<n>" */
static char *vt_path(int n)
{
    static char path[] = "/dev/ttyN";
    path[8] = '0' + n;
    return path;
}

static int vt_active(int fd)
{
    struct vt_stat vs = {0};
    SC(SYS_ioctl, fd, VT_GETSTATE, &vs);
    return vs.v_active;
}
static void child_handler(int sig) { SC(SYS_exit, 7, 0, 0); (void) sig; }


static volatile int thread_ran = 0;
static void thr(void *u, void *a) { (void) u; (void) a; thread_ran = 1; SC(SYS_thread_exit, 0, 0, 0); for (;;); }
static char tstack[8192] __attribute__((aligned(16)));

static unsigned char bad_elf[52 + 32];

static int hup_pipe = -1;
static void hup_handler(int sig) { SC(SYS_write, hup_pipe, "H", 1); SC(SYS_exit, 0, 0, 0); (void) sig; }

/* "/dev/pts/<n>" */
static char *pts_path(int n)
{
    static char path[16] = "/dev/pts/";
    int i = 9;

    if (n >= 10)
        path[i++] = '0' + n / 10;
    path[i++] = '0' + n % 10;
    path[i] = 0;
    return path;
}

/* a new session with a pty as its controlling terminal; exits with the
 * failed step */
static void pty_session(void)
{
    int me = SC(SYS_getpid, 0, 0, 0), pgid = 0, n = -1;

    if (SC(SYS_setsid, 0, 0, 0) != me) SC(SYS_exit, 1, 0, 0);
    if (SC(SYS_open, "/dev/tty", O_RDWR, 0) != -ENXIO) SC(SYS_exit, 2, 0, 0);

    int master = SC(SYS_open, "/dev/ptmx", O_RDWR, 0);
    if (master < 0 || SC(SYS_ioctl, master, TIOCGPTN, &n) || n < 0) SC(SYS_exit, 3, 0, 0);

    /* opening the slave makes it the controlling terminal */
    int slave = SC(SYS_open, pts_path(n), O_RDWR, 0);
    if (slave < 0) SC(SYS_exit, 4, 0, 0);
    if (SC(SYS_ioctl, slave, TIOCGPGRP, &pgid) || pgid != me) SC(SYS_exit, 5, 0, 0);

    /* /dev/tty is that pty */
    int tty = SC(SYS_open, "/dev/tty", O_WRONLY, 0);
    if (tty < 0) SC(SYS_exit, 6, 0, 0);
    char out[8] = {0};
    SC(SYS_write, tty, "via", 3);
    if (SC(SYS_read, master, out, 8) != 3 || out[0] != 'v') SC(SYS_exit, 7, 0, 0);

    /* a foreground group that goes away leaves none */
    int c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        SC(SYS_setpgid, 0, 0, 0);
        SC(SYS_exit, 0, 0, 0);
    }
    aq_spin(10000000);
    if (SC(SYS_ioctl, slave, TIOCSPGRP, &c)) SC(SYS_exit, 8, 0, 0);
    int st;
    SC(SYS_waitpid, c, &st, 0);
    SC(SYS_ioctl, slave, TIOCGPGRP, &pgid);
    if (pgid != 0x7fffffff) SC(SYS_exit, 9, 0, 0);
    SC(SYS_ioctl, slave, TIOCSPGRP, &me);

    /* the end of the session hangs up the foreground group: a child left
     * in it writes to hup_pipe when it gets SIGHUP */
    int ready[2];
    SC(SYS_pipe, ready, 0, 0);
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        struct aquila_sigaction hsa = {(u32) hup_handler, 0, 0};
        SC(SYS_sigaction, SIGHUP, &hsa, 0);
        SC(SYS_write, ready[1], "R", 1);
        for (;;)
            SC(SYS_getpid, 0, 0, 0);
    }
    char r;
    SC(SYS_read, ready[0], &r, 1);
    SC(SYS_exit, 0, 0, 0);
}

static void spin_forever(void)
{
    for (;;)
        SC(SYS_getpid, 0, 0, 0);
}

static void sigtstp_handler(int sig) { SC(SYS_exit, 9, 0, 0); (void) sig; }

/* the line discipline of a pty that is the controlling terminal of a new
 * session; exits with the failed step */
static void line_discipline(void)
{
    int n = -1, me = SC(SYS_getpid, 0, 0, 0), st;
    char buf[32];

    SC(SYS_setsid, 0, 0, 0);
    int master = SC(SYS_open, "/dev/ptmx", O_RDWR, 0);
    SC(SYS_ioctl, master, TIOCGPTN, &n);
    int slave = SC(SYS_open, pts_path(n), O_RDWR, 0);
    if (master < 0 || slave < 0) SC(SYS_exit, 1, 0, 0);

    /* ^D: the line so far without a newline, then end-of-file */
    SC(SYS_write, master, "ab\x04", 3);
    if (SC(SYS_read, slave, buf, sizeof(buf)) != 2 || buf[1] != 'b') SC(SYS_exit, 2, 0, 0);
    SC(SYS_write, master, "\x04", 1);
    if (SC(SYS_read, slave, buf, sizeof(buf)) != 0) SC(SYS_exit, 3, 0, 0);

    /* erase (DEL) and kill (^U) */
    SC(SYS_write, master, "q\x7fz\x15xy\n", 7);
    if (SC(SYS_read, slave, buf, sizeof(buf)) != 3 || buf[0] != 'x' || buf[2] != '\n') SC(SYS_exit, 4, 0, 0);

    /* more than one line in a write: nothing is lost */
    SC(SYS_write, master, "1\n2\n", 4);
    int got = 0, r;
    while (got < 4 && (r = SC(SYS_read, slave, buf + got, sizeof(buf) - got)) > 0)
        got += r;
    if (got != 4 || buf[2] != '2') SC(SYS_exit, 5, 0, 0);

    /* non-canonical with VMIN 0 doesn't wait */
    struct termios t, raw;
    SC(SYS_ioctl, slave, TCGETS, &t);
    raw = t;
    raw.c_lflag &= ~ICANON;
    raw.c_cc[VMIN] = 0;
    SC(SYS_ioctl, slave, TCSETS, &raw);
    if (SC(SYS_read, slave, buf, sizeof(buf)) != 0) SC(SYS_exit, 6, 0, 0);
    SC(SYS_write, master, "k", 1);
    if (SC(SYS_read, slave, buf, sizeof(buf)) != 1 || buf[0] != 'k') SC(SYS_exit, 7, 0, 0);
    SC(SYS_ioctl, slave, TCSETS, &t);

    /* a background reader is stopped by SIGTTIN */
    int c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        SC(SYS_setpgid, 0, 0, 0);
        SC(SYS_read, slave, buf, 1);
        SC(SYS_exit, 0, 0, 0);
    }
    SC(SYS_waitpid, c, &st, WUNTRACED);
    if ((st & 0xff) != 0x7f || (st >> 8) != SIGTTIN) SC(SYS_exit, 8, 0, 0);
    SC(SYS_kill, c, SIGKILL, 0);
    SC(SYS_waitpid, c, &st, 0);

    /* ^Z stops the foreground group, ^C kills it */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        SC(SYS_setpgid, 0, 0, 0);
        spin_forever();
    }
    SC(SYS_setpgid, c, c, 0);
    SC(SYS_ioctl, slave, TIOCSPGRP, &c);
    SC(SYS_write, master, "\x1a", 1);
    SC(SYS_waitpid, c, &st, WUNTRACED);
    if ((st & 0xff) != 0x7f || (st >> 8) != SIGTSTP) SC(SYS_exit, 10, 0, 0);
    SC(SYS_kill, c, SIGCONT, 0);
    SC(SYS_write, master, "\x03", 1);
    SC(SYS_waitpid, c, &st, 0);
    if ((st & 0x7f) != SIGINT) SC(SYS_exit, 11, 0, 0);
    SC(SYS_ioctl, slave, TIOCSPGRP, &me);

    SC(SYS_exit, 0, 0, 0);
}

/* select() sets, as the kernel takes them (FD_SETSIZE 64) */
struct fdset { unsigned long bits[2]; };

static int do_select(int nfds, struct fdset *r, struct fdset *w, long usec)
{
    struct { u32 sec_lo, sec_hi; long usec; } tv = {usec / 1000000, 0, usec % 1000000};
    struct aquila_select_args a = {nfds, r, w, 0, usec < 0 ? 0 : &tv};
    return SC(SYS_select, &a, 0, 0);
}

static int now_ms(void);

/* a raw read on a pty with VMIN 0 and VTIME 3 waits 300ms for input;
 * exits with the failed step */
static void read_timeout(void)
{
    int n = -1;
    char buf[8];

    SC(SYS_setsid, 0, 0, 0);
    int master = SC(SYS_open, "/dev/ptmx", O_RDWR, 0);
    SC(SYS_ioctl, master, TIOCGPTN, &n);
    int slave = SC(SYS_open, pts_path(n), O_RDWR, 0);

    struct termios t;
    SC(SYS_ioctl, slave, TCGETS, &t);
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 3;
    SC(SYS_ioctl, slave, TCSETS, &t);

    int t0 = now_ms();
    if (SC(SYS_read, slave, buf, sizeof(buf)) != 0) SC(SYS_exit, 1, 0, 0);
    int waited = now_ms() - t0;
    if (waited < 280 || waited > 1000) SC(SYS_exit, 2, 0, 0);

    /* input before the time is up ends the wait */
    int c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        struct aquila_timespec d = {0, 100000000};
        SC(SYS_nanosleep, &d, 0, 0);
        SC(SYS_write, master, "z", 1);
        SC(SYS_exit, 0, 0, 0);
    }
    t0 = now_ms();
    if (SC(SYS_read, slave, buf, sizeof(buf)) != 1 || buf[0] != 'z') SC(SYS_exit, 3, 0, 0);
    if (now_ms() - t0 >= 280) SC(SYS_exit, 4, 0, 0);

    /* select sees the master readable once the terminal writes */
    struct fdset r = {{1UL << master, 0}};
    if (do_select(master + 1, &r, 0, 0) != 0) SC(SYS_exit, 5, 0, 0);
    SC(SYS_write, slave, "out", 3);
    r.bits[0] = 1UL << master;
    if (do_select(master + 1, &r, 0, 0) != 1) SC(SYS_exit, 6, 0, 0);

    SC(SYS_exit, 0, 0, 0);
}

/* CLOCK_MONOTONIC in milliseconds */
static int now_ms(void)
{
    struct aquila_timespec ts;
    SC(SYS_clock_gettime, CLOCK_MONOTONIC, &ts, 0);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#define ST(buf) ((struct aquila_stat *) (buf))

/* as an unprivileged user (uid 1000) in /tmp, exits with the failed step */
static void unprivileged_files(void)
{
    char st[128];

    if (SC(SYS_setgid, 100, 0, 0) || SC(SYS_setuid, 1000, 0, 0)) SC(SYS_exit, 1, 0, 0);

    /* /tmp/rootdir is root's, 0755 */
    if (SC(SYS_open, "/tmp/rootdir/new", O_CREAT | O_WRONLY, 0644) != -EACCES) SC(SYS_exit, 2, 0, 0);
    if (SC(SYS_unlink, "/tmp/rootdir/file", 0, 0) != -EACCES) SC(SYS_exit, 3, 0, 0);
    if (SC(SYS_chmod, "/tmp/rootdir/file", 0777, 0) != -EPERM) SC(SYS_exit, 4, 0, 0);
    if (SC5(SYS_utimensat, AT_FDCWD, "/tmp/rootdir/file", 0, 0, 0) != -EACCES) SC(SYS_exit, 5, 0, 0);
    if (SC(SYS_mknod, "/tmp/dev", S_IFCHR | 0666, DEV(4, 64)) != -EPERM) SC(SYS_exit, 6, 0, 0);

    /* own files */
    int fd = SC(SYS_open, "/tmp/mine", O_CREAT | O_WRONLY, 0644);
    if (fd < 0) SC(SYS_exit, 7, 0, 0);
    SC(SYS_fstat, fd, st, 0);
    if (ST(st)->st_uid != 1000 || ST(st)->st_gid != 100) SC(SYS_exit, 8, 0, 0);
    if (SC(SYS_fchmod, fd, 0600, 0)) SC(SYS_exit, 9, 0, 0);
    /* giving it away */
    if (SC(SYS_chown, "/tmp/mine", 0, -1) != -EPERM) SC(SYS_exit, 10, 0, 0);
    if (SC(SYS_chown, "/tmp/mine", -1, 100)) SC(SYS_exit, 11, 0, 0);
    if (SC(SYS_chown, "/tmp/mine", -1, 0) != -EPERM) SC(SYS_exit, 12, 0, 0);
    SC(SYS_close, fd, 0, 0);
    if (SC(SYS_unlink, "/tmp/mine", 0, 0)) SC(SYS_exit, 13, 0, 0);
    /* only root restarts the machine */
    if (SC(SYS_reboot, RB_AUTOBOOT, 0, 0) != -EPERM) SC(SYS_exit, 14, 0, 0);

    SC(SYS_exit, 0, 0, 0);
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        ++a, ++b;

    return *a == *b;
}

/* run as the interpreter of /tmp/script (see the execve tests): argc,
 * argv and envp are on the entry stack */
static void script_interpreter(void)
{
    u32 *entry = (u32 *) aq_entry_esp;
    char **argv = (char **) entry[1];

    if (entry[0] != 4 || !str_eq(argv[1], "-x y"))
        return;

    SC(SYS_exit, str_eq(argv[0], "/init") && str_eq(argv[2], "/tmp/script") &&
                 str_eq(argv[3], "arg") ? 42 : 1, 0, 0);
}

void aq_main(void)
{
    script_interpreter();
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
    /* more than FD_SETSIZE */
    check("select huge nfds", SC(SYS_select, &sel, 0, 0), -EINVAL);
    check("select kargs", SC(SYS_select, KADDR, 0, 0), -EFAULT);

    /* mmap */
    void *mret;
    struct aquila_mmap_args ma = {(void *) 0xC0000000, 0x1000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE, aq_out, 0};
    check("mmap fixed kernel", SC(SYS_mmap, &ma, &mret, 0), -EINVAL);
    check("mmap kret", SC(SYS_mmap, &ma, KADDR, 0), -EFAULT);

    /* anonymous memory: zero-filled pages, private to the process */
    struct aquila_mmap_args aa = {0, 100, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0};
    check("mmap anonymous", SC(SYS_mmap, &aa, &mret, 0), 0);
    volatile char *anon = mret;
    check("mmap anonymous page aligned", (u32) anon & 0xfff, 0);
    check("mmap anonymous zero", anon[0] | anon[99] | anon[4095], 0);
    anon[0] = 'p';
    int ac = SC(SYS_fork, 0, 0, 0);
    if (!ac) {
        int ok = anon[0] == 'p';
        anon[0] = 'c';
        SC(SYS_exit, ok, 0, 0);
    }
    int ast = 0;
    SC(SYS_waitpid, ac, &ast, 0);
    check("mmap anonymous copied by fork", ast >> 8, 1);
    check("mmap anonymous private", anon[0], 'p');
    check("munmap anonymous", SC(SYS_munmap, anon, 100, 0), 0);
    check("munmap again", SC(SYS_munmap, anon, 100, 0), -EINVAL);
    aa.flags = MAP_SHARED | MAP_ANONYMOUS;
    check("mmap anonymous shared", SC(SYS_mmap, &aa, &mret, 0), -EINVAL);
    aa.flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
    aa.addr = (void *) 0x40000100;
    check("mmap fixed unaligned", SC(SYS_mmap, &aa, &mret, 0), -EINVAL);

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

    /* execute permission */
    SC(SYS_chmod, "/tmp/bad", 0644, 0);
    check("execve without execute permission", SC(SYS_execve, "/tmp/bad", 0, 0), -EACCES);
    check("execve directory", SC(SYS_execve, "/tmp", 0, 0), -EACCES);

    /* directories are read with readdir, not read */
    const char *dirs[] = {"/", "/tmp", "/dev"};
    for (int i = 0; i < 3; ++i) {
        char dbuf[16];
        int dfd = SC(SYS_open, dirs[i], O_RDONLY, 0);
        check("read directory", SC(SYS_read, dfd, dbuf, sizeof(dbuf)), -EISDIR);
        SC(SYS_close, dfd, 0, 0);
    }
    check("open directory for writing", SC(SYS_open, "/tmp", O_WRONLY, 0), -EISDIR);

    /* scripts: "#!interpreter argument", this program is the interpreter */
    fd = SC(SYS_open, "/tmp/script", O_CREAT | O_RDWR, 0755);
    SC(SYS_write, fd, "#! /init -x y \nignored\n", 23);
    SC(SYS_close, fd, 0, 0);
    char *script_argv[] = {"script", "arg", 0};
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) { SC(SYS_execve, "/tmp/script", script_argv, 0); SC(SYS_exit, 99, 0, 0); }
    SC(SYS_waitpid, c, &st, 0);
    check("execve script", st >> 8, 42);
    SC(SYS_chmod, "/tmp/script", 0644, 0);
    check("execve script without execute permission", SC(SYS_execve, "/tmp/script", script_argv, 0), -EACCES);
    fd = SC(SYS_open, "/tmp/loop", O_CREAT | O_RDWR, 0755);
    SC(SYS_write, fd, "#!/tmp/loop\n", 12);
    SC(SYS_close, fd, 0, 0);
    check("execve script loop", SC(SYS_execve, "/tmp/loop", script_argv, 0), -ELOOP);
    fd = SC(SYS_open, "/tmp/noint", O_CREAT | O_RDWR, 0755);
    SC(SYS_write, fd, "#!  \n", 5);
    SC(SYS_close, fd, 0, 0);
    check("execve script without interpreter", SC(SYS_execve, "/tmp/noint", script_argv, 0), -ENOEXEC);

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

    /* --- files (on the tmpfs mounted at /tmp) --- */
    char sb[128];
    check("umask", SC(SYS_umask, 022, 0, 0), 022);
    fd = SC(SYS_open, "/tmp/a", O_CREAT | O_WRONLY, 0666);
    check("create", fd >= 0, 1);
    SC(SYS_write, fd, "hello", 5);
    SC(SYS_fstat, fd, sb, 0);
    check("umask applied", ST(sb)->st_mode & 07777, 0644);
    SC(SYS_close, fd, 0, 0);
    check("O_EXCL existing", SC(SYS_open, "/tmp/a", O_CREAT | O_EXCL | O_WRONLY, 0644), -EEXIST);
    check("O_DIRECTORY on a file", SC(SYS_open, "/tmp/a", O_RDONLY | O_DIRECTORY, 0), -ENOTDIR);

    check("mkdir", SC(SYS_mkdir, "/tmp/d", 0755, 0), 0);
    check("mkdir existing", SC(SYS_mkdir, "/tmp/d", 0755, 0), -EEXIST);
    check("mkdir mountpoint", SC(SYS_mkdir, "/tmp", 0755, 0), -EEXIST);
    check("open directory for writing", SC(SYS_open, "/tmp/d", O_WRONLY, 0), -EISDIR);
    int dirfd = SC(SYS_open, "/tmp/d", O_RDONLY | O_DIRECTORY, 0);
    check("open directory", dirfd >= 0, 1);
    fd = SC5(SYS_openat, dirfd, "f", O_CREAT | O_WRONLY, 0644, 0);
    check("openat", fd >= 0, 1);
    SC(SYS_close, fd, 0, 0);
    check("created in the directory", SC(SYS_stat, "/tmp/d/f", sb, 0), 0);
    check("fstatat", SC5(SYS_fstatat, dirfd, "f", sb, 0, 0), 0);
    check("fstatat regular", ST(sb)->st_mode & S_IFMT, S_IFREG);
    check("fstatat AT_FDCWD", SC5(SYS_fstatat, AT_FDCWD, "/tmp/d/f", sb, 0, 0), 0);
    check("openat bad dirfd", SC5(SYS_openat, 50, "f", O_RDONLY, 0, 0), -EBADF);
    fd = SC(SYS_open, "/tmp/a", O_RDONLY, 0);
    check("openat file as dirfd", SC5(SYS_openat, fd, "f", O_RDONLY, 0, 0), -ENOTDIR);
    SC(SYS_close, fd, 0, 0);
    check("openat absolute ignores dirfd", SC5(SYS_fstatat, 50, "/tmp/a", sb, 0, 0), 0);
    check("mknodat", SC5(SYS_mknodat, dirfd, "sub", S_IFDIR | 0755, 0, 0), 0);
    check("faccessat", SC5(SYS_faccessat, dirfd, "f", 4 | 2 /* R_OK | W_OK */, 0, 0), 0);
    check("root X_OK without x bits", SC5(SYS_faccessat, dirfd, "f", 1 /* X_OK */, 0, 0), -EACCES);

    /* the directory descriptor survives fork and dup */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        int dup = SC(SYS_fcntl, dirfd, F_DUPFD, 30);
        SC(SYS_close, dirfd, 0, 0);
        SC(SYS_exit, SC5(SYS_fstatat, dup, "f", sb, 0, 0) == 0 ? 3 : 4, 0, 0);
    }
    SC(SYS_waitpid, c, &st, 0);
    check("dirfd after fork and dup", st >> 8, 3);

    char cwd[64];
    check("fchdir", SC(SYS_fchdir, dirfd, 0, 0), 0);
    SC(SYS_getcwd, cwd, sizeof(cwd), 0);
    check("cwd after fchdir", cwd[0] == '/' && cwd[5] == 'd' && cwd[6] == 0, 1);
    check("relative open in new cwd", SC(SYS_stat, "f", sb, 0), 0);
    SC(SYS_chdir, "/", 0, 0);

    check("unlink a directory", SC(SYS_unlink, "/tmp/d", 0, 0), -EISDIR);
    check("rmdir not empty", SC(SYS_rmdir, "/tmp/d", 0, 0), -ENOTEMPTY);
    check("rmdir a file", SC(SYS_rmdir, "/tmp/d/f", 0, 0), -ENOTDIR);
    check("rmdir mountpoint", SC(SYS_rmdir, "/tmp", 0, 0), -EBUSY);
    check("unlinkat AT_REMOVEDIR", SC(SYS_unlinkat, dirfd, "sub", AT_REMOVEDIR), 0);

    /* hard links */
    check("link", SC(SYS_link, "/tmp/a", "/tmp/d/b", 0), 0);
    SC(SYS_stat, "/tmp/a", sb, 0);
    check("nlink 2", ST(sb)->st_nlink, 2);
    check("link existing", SC(SYS_link, "/tmp/a", "/tmp/d/b", 0), -EEXIST);
    check("link a directory", SC(SYS_link, "/tmp/d", "/tmp/e", 0), -EPERM);
    check("link across filesystems", SC(SYS_link, "/tmp/a", "/a", 0), -EXDEV);
    check("unlink first name", SC(SYS_unlink, "/tmp/a", 0, 0), 0);
    fd = SC(SYS_open, "/tmp/d/b", O_RDONLY, 0);
    char data[8] = {0};
    check("other name still reads", SC(SYS_read, fd, data, 8), 5);
    SC(SYS_close, fd, 0, 0);

    /* symbolic links */
    check("symlinkat", SC(SYS_symlinkat, "b", dirfd, "s"), 0);
    check("symlink absolute", SC(SYS_symlinkat, "/tmp/d", AT_FDCWD, "/tmp/dl"), 0);
    char link[16] = {0};
    check("readlinkat", SC5(SYS_readlinkat, dirfd, "s", link, sizeof(link), 0), 1);
    check("readlinkat contents", link[0], 'b');
    check("readlink not a link", SC5(SYS_readlinkat, dirfd, "b", link, sizeof(link), 0), -EINVAL);
    check("symlink existing", SC(SYS_symlinkat, "x", dirfd, "s"), -EEXIST);
    SC5(SYS_fstatat, dirfd, "s", sb, AT_SYMLINK_NOFOLLOW, 0);
    check("lstat link", ST(sb)->st_mode & S_IFMT, S_IFLNK);
    SC5(SYS_fstatat, dirfd, "s", sb, 0, 0);
    check("stat through link", ST(sb)->st_mode & S_IFMT, S_IFREG);
    check("O_NOFOLLOW", SC5(SYS_openat, dirfd, "s", O_RDONLY | O_NOFOLLOW, 0, 0), -ELOOP);
    check("link in the middle of a path", SC(SYS_stat, "/tmp/dl/b", sb, 0), 0);
    check("link to a link's directory", SC(SYS_stat, "/tmp/dl/s", sb, 0), 0);
    check("create through a link", SC(SYS_mkdir, "/tmp/dl/viadl", 0755, 0), 0);
    check("made in the target", SC(SYS_rmdir, "/tmp/d/viadl", 0, 0), 0);
    /* O_CREAT through a dangling link creates what it points to */
    SC(SYS_symlinkat, "/tmp/d/made", AT_FDCWD, "/tmp/dangling");
    fd = SC(SYS_open, "/tmp/dangling", O_CREAT | O_WRONLY, 0644);
    check("create through a dangling link", fd >= 0, 1);
    SC(SYS_close, fd, 0, 0);
    check("created the target", SC(SYS_stat, "/tmp/d/made", sb, 0), 0);
    SC(SYS_symlinkat, "rel", dirfd, "rl");
    SC(SYS_symlinkat, "/tmp/d/rl", AT_FDCWD, "/tmp/chain");
    fd = SC(SYS_open, "/tmp/chain", O_CREAT | O_WRONLY, 0644);
    check("create through links to a relative link", fd >= 0, 1);
    SC(SYS_close, fd, 0, 0);
    check("relative to the link's directory", SC(SYS_stat, "/tmp/d/rel", sb, 0), 0);
    SC(SYS_symlinkat, "/tmp/d/excl", AT_FDCWD, "/tmp/dangling2");
    check("O_EXCL doesn't create through a link", SC(SYS_open, "/tmp/dangling2", O_CREAT | O_EXCL | O_WRONLY, 0644), -EEXIST);
    check("nothing created", SC(SYS_stat, "/tmp/d/excl", sb, 0), -ENOENT);
    SC(SYS_unlink, "/tmp/d/made", 0, 0);
    SC(SYS_unlink, "/tmp/d/rel", 0, 0);
    SC(SYS_unlink, "/tmp/d/rl", 0, 0);
    SC(SYS_unlink, "/tmp/dangling", 0, 0);
    SC(SYS_unlink, "/tmp/dangling2", 0, 0);
    SC(SYS_unlink, "/tmp/chain", 0, 0);

    /* rename */
    check("rename", SC5(SYS_renameat, dirfd, "b", AT_FDCWD, "/tmp/c", 0), 0);
    check("old name gone", SC(SYS_stat, "/tmp/d/b", sb, 0), -ENOENT);
    check("new name there", SC(SYS_stat, "/tmp/c", sb, 0), 0);
    fd = SC(SYS_open, "/tmp/other", O_CREAT | O_WRONLY, 0644);
    SC(SYS_close, fd, 0, 0);
    check("rename over a file", SC5(SYS_renameat, AT_FDCWD, "/tmp/c", AT_FDCWD, "/tmp/other", 0), 0);
    SC(SYS_stat, "/tmp/other", sb, 0);
    check("replaced", ST(sb)->st_size, 5);
    check("rename a file over a directory", SC5(SYS_renameat, AT_FDCWD, "/tmp/other", AT_FDCWD, "/tmp/d", 0), -EISDIR);
    SC(SYS_mkdir, "/tmp/e", 0755, 0);
    check("rename a directory into itself", SC5(SYS_renameat, AT_FDCWD, "/tmp/d", AT_FDCWD, "/tmp/d/x", 0), -EINVAL);
    check("rename over a non-empty directory", SC5(SYS_renameat, AT_FDCWD, "/tmp/e", AT_FDCWD, "/tmp/d", 0), -ENOTEMPTY);
    check("rename a directory", SC5(SYS_renameat, AT_FDCWD, "/tmp/d", AT_FDCWD, "/tmp/e", 0), 0);
    check("moved with its contents", SC(SYS_stat, "/tmp/e/f", sb, 0), 0);
    check("rename across filesystems", SC5(SYS_renameat, AT_FDCWD, "/tmp/other", AT_FDCWD, "/other", 0), -EXDEV);
    check("rename missing", SC5(SYS_renameat, AT_FDCWD, "/tmp/nothing", AT_FDCWD, "/tmp/x", 0), -ENOENT);

    /* attributes */
    check("chmod", SC(SYS_chmod, "/tmp/other", 04751, 0), 0);
    SC(SYS_stat, "/tmp/other", sb, 0);
    check("mode changed", ST(sb)->st_mode, S_IFREG | 04751);
    check("chmod read-only fs", SC(SYS_chmod, "/init", 0700, 0), -EROFS);
    SC(SYS_symlinkat, "other", AT_FDCWD, "/tmp/ol");
    check("chmod a link's target", SC5(SYS_fchmodat, AT_FDCWD, "/tmp/ol", 04751, 0, 0), 0);
    check("fchmodat AT_SYMLINK_NOFOLLOW", SC5(SYS_fchmodat, AT_FDCWD, "/tmp/ol", 0700, AT_SYMLINK_NOFOLLOW, 0), -EOPNOTSUPP);
    check("chown", SC(SYS_chown, "/tmp/other", 1000, 100), 0);
    SC(SYS_stat, "/tmp/other", sb, 0);
    check("owner changed", ST(sb)->st_uid == 1000 && ST(sb)->st_gid == 100, 1);
    check("lchown", SC(SYS_lchown, "/tmp/ol", 2000, -1), 0);
    SC5(SYS_fstatat, AT_FDCWD, "/tmp/ol", sb, AT_SYMLINK_NOFOLLOW, 0);
    check("link owner changed", ST(sb)->st_uid, 2000);
    SC(SYS_stat, "/tmp/ol", sb, 0);
    check("target owner unchanged", ST(sb)->st_uid, 1000);

    struct aquila_timespec times[2] = {{100, 0}, {200, 0}};
    check("utimensat", SC5(SYS_utimensat, AT_FDCWD, "/tmp/other", times, 0, 0), 0);
    SC(SYS_stat, "/tmp/other", sb, 0);
    check("times set", ST(sb)->st_atim.tv_sec == 100 && ST(sb)->st_mtim.tv_sec == 200, 1);
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_nsec = UTIME_NOW;
    SC5(SYS_utimensat, AT_FDCWD, "/tmp/other", times, 0, 0);
    SC(SYS_stat, "/tmp/other", sb, 0);
    check("UTIME_OMIT and UTIME_NOW", ST(sb)->st_atim.tv_sec == 100 && ST(sb)->st_mtim.tv_sec > 200, 1);
    times[1].tv_nsec = 1000000000;
    check("utimensat bad nsec", SC5(SYS_utimensat, AT_FDCWD, "/tmp/other", times, 0, 0), -EINVAL);
    fd = SC(SYS_open, "/tmp/other", O_RDONLY, 0);
    times[1].tv_nsec = 0;
    check("futimens", SC5(SYS_utimensat, fd, 0, times, 0, 0), 0);
    SC(SYS_close, fd, 0, 0);
    SC(SYS_close, dirfd, 0, 0);

    /* permissions of an unprivileged user */
    SC(SYS_mkdir, "/tmp/rootdir", 0755, 0);
    fd = SC(SYS_open, "/tmp/rootdir/file", O_CREAT | O_WRONLY, 0644);
    SC(SYS_close, fd, 0, 0);
    check("unprivileged file rules (failed step)", aq_child_status(unprivileged_files) >> 8, 0);

    /* setsid: not as a group leader */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        int me = SC(SYS_getpid, 0, 0, 0);
        if (SC(SYS_setsid, 0, 0, 0) != me) SC(SYS_exit, 1, 0, 0);
        if (SC(SYS_getpgrp, 0, 0, 0) != me) SC(SYS_exit, 2, 0, 0);
        if (SC(SYS_setsid, 0, 0, 0) != -EPERM) SC(SYS_exit, 3, 0, 0);
        SC(SYS_exit, 0, 0, 0);
    }
    SC(SYS_waitpid, c, &st, 0);
    check("setsid (failed step)", st >> 8, 0);
    check("setsid as a group leader", SC(SYS_setsid, 0, 0, 0), -EPERM);

    /* --- FIFOs --- */
    check("mkfifo", SC(SYS_mknod, "/tmp/fifo", S_IFIFO | 0666, 0), 0);
    SC(SYS_stat, "/tmp/fifo", sb, 0);
    check("stat fifo", ST(sb)->st_mode & S_IFMT, S_IFIFO);
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        /* waits for the reader */
        int w = SC(SYS_open, "/tmp/fifo", O_WRONLY, 0);
        SC(SYS_write, w, "fifo", 4);
        SC(SYS_exit, w >= 0 ? 0 : 1, 0, 0);
    }
    aq_spin(10000000);
    fd = SC(SYS_open, "/tmp/fifo", O_RDONLY, 0);
    check("open fifo for reading", fd >= 0, 1);
    char fbuf[8] = {0};
    check("read fifo", SC(SYS_read, fd, fbuf, 8), 4);
    check("fifo data", fbuf[0] == 'f' && fbuf[3] == 'o', 1);
    check("fifo EOF after the writer", SC(SYS_read, fd, fbuf, 8), 0);
    SC(SYS_close, fd, 0, 0);
    SC(SYS_waitpid, c, &st, 0);
    check("fifo writer", st >> 8, 0);
    check("nonblocking writer without readers", SC(SYS_open, "/tmp/fifo", O_WRONLY | O_NONBLOCK, 0), -ENXIO);
    fd = SC(SYS_open, "/tmp/fifo", O_RDONLY | O_NONBLOCK, 0);
    check("nonblocking reader", fd >= 0, 1);
    check("nonblocking reader EOF", SC(SYS_read, fd, fbuf, 8), 0);
    SC(SYS_close, fd, 0, 0);
    fd = SC(SYS_open, "/tmp/fifo", O_RDWR, 0);
    check("fifo O_RDWR", fd >= 0, 1);
    check("O_RDWR write", SC(SYS_write, fd, "rw", 2), 2);
    check("O_RDWR read", SC(SYS_read, fd, fbuf, 8), 2);
    SC(SYS_close, fd, 0, 0);

    /* a reader interrupted while waiting leaves no reader behind */
    sa.sa_handler = (u32) handler;
    SC(SYS_sigaction, SIGUSR1, &sa, 0);
    c = SC(SYS_fork, 0, 0, 0);
    if (!c)
        SC(SYS_exit, SC(SYS_open, "/tmp/fifo", O_RDONLY, 0) == -EINTR && got_sig == SIGUSR1 ? 0 : 1, 0, 0);
    aq_spin(10000000);
    SC(SYS_kill, c, SIGUSR1, 0);
    SC(SYS_waitpid, c, &st, 0);
    check("fifo open interrupted", st >> 8, 0);
    check("no reader left", SC(SYS_open, "/tmp/fifo", O_WRONLY | O_NONBLOCK, 0), -ENXIO);
    check("unlink fifo", SC(SYS_unlink, "/tmp/fifo", 0, 0), 0);

    /* --- sync: what the disks have cached (the disk test reads it back) --- */
    check("sync", SC(SYS_sync, 0, 0, 0), 0);

    /* --- reboot (restarting itself is left to the system test) --- */
    check("reboot bad command", SC(SYS_reboot, 0x12345678, 0, 0), -EINVAL);
    check("reboot disable Ctrl+Alt+Del", SC(SYS_reboot, RB_DISABLE_CAD, 0, 0), 0);
    check("reboot enable Ctrl+Alt+Del", SC(SYS_reboot, RB_ENABLE_CAD, 0, 0), 0);

    /* --- flock --- */
    int la = SC(SYS_open, "/tmp/lock", O_CREAT | O_RDWR, 0644);
    int lb = SC(SYS_open, "/tmp/lock", O_RDWR, 0);
    check("flock bad fd", SC(SYS_flock, 99, LOCK_EX, 0), -EBADF);
    check("flock bad op", SC(SYS_flock, la, 0, 0), -EINVAL);
    check("flock exclusive", SC(SYS_flock, la, LOCK_EX, 0), 0);
    check("flock exclusive again", SC(SYS_flock, la, LOCK_EX | LOCK_NB, 0), 0);
    check("flock held by another open", SC(SYS_flock, lb, LOCK_EX | LOCK_NB, 0), -EAGAIN);
    check("flock shared vs exclusive", SC(SYS_flock, lb, LOCK_SH | LOCK_NB, 0), -EAGAIN);
    /* a copy of the descriptor holds the same lock */
    int lc = SC(SYS_fcntl, la, F_DUPFD, 0);
    SC(SYS_close, la, 0, 0);
    check("flock kept by the copy", SC(SYS_flock, lb, LOCK_EX | LOCK_NB, 0), -EAGAIN);
    SC(SYS_close, lc, 0, 0);
    check("flock released on the last close", SC(SYS_flock, lb, LOCK_EX | LOCK_NB, 0), 0);
    /* shared locks */
    check("flock downgrade", SC(SYS_flock, lb, LOCK_SH, 0), 0);
    la = SC(SYS_open, "/tmp/lock", O_RDONLY, 0);
    check("flock two shared", SC(SYS_flock, la, LOCK_SH | LOCK_NB, 0), 0);
    check("flock exclusive vs shared", SC(SYS_flock, la, LOCK_EX | LOCK_NB, 0), -EAGAIN);
    check("flock unlock", SC(SYS_flock, lb, LOCK_UN, 0), 0);
    check("flock upgrade", SC(SYS_flock, la, LOCK_EX | LOCK_NB, 0), 0);
    /* waiting for the lock: the child gets it once it is released */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        int l = SC(SYS_open, "/tmp/lock", O_RDWR, 0);
        int busy = SC(SYS_flock, l, LOCK_EX | LOCK_NB, 0) == -EAGAIN;
        SC(SYS_exit, busy && SC(SYS_flock, l, LOCK_EX, 0) == 0 ? 0 : 1, 0, 0);
    }
    aq_spin(10000000);
    SC(SYS_flock, la, LOCK_UN, 0);
    SC(SYS_waitpid, c, &st, 0);
    check("flock waits for the holder", st >> 8, 0);
    SC(SYS_close, la, 0, 0);
    SC(SYS_close, lb, 0, 0);
    SC(SYS_unlink, "/tmp/lock", 0, 0);

    int fp[2];
    SC(SYS_pipe, fp, 0, 0);
    SC(SYS_fstat, fp[0], sb, 0);
    check("pipe is a FIFO", ST(sb)->st_mode & S_IFMT, S_IFIFO);
    SC(SYS_close, fp[0], 0, 0);
    SC(SYS_close, fp[1], 0, 0);

    /* --- sessions and controlling terminals --- */
    SC(SYS_mknod, "/dev/tty", S_IFCHR | 0666, DEV(5, 0));
    SC(SYS_mknod, "/dev/ptmx", S_IFCHR | 0666, DEV(5, 2));
    SC(SYS_mkdir, "/dev/pts", 0755, 0);
    check("mount devpts", aq_mount("devpts", "/dev/pts", 0), 0);
    int hp[2];
    SC(SYS_pipe, hp, 0, 0);
    hup_pipe = hp[1];
    c = SC(SYS_fork, 0, 0, 0);
    if (!c)
        pty_session();
    SC(SYS_close, hp[1], 0, 0);
    SC(SYS_waitpid, c, &st, 0);
    check("pty session (failed step)", st >> 8, 0);
    char hup = 0;
    check("SIGHUP to the foreground group", SC(SYS_read, hp[0], &hup, 1) == 1 && hup == 'H', 1);
    SC(SYS_close, hp[0], 0, 0);

    /* every end closed: the pty went away, its id is free again */
    aq_spin(10000000);
    check("/dev/pts/0 removed", SC(SYS_stat, "/dev/pts/0", sb, 0), -ENOENT);
    int ptm = SC(SYS_open, "/dev/ptmx", O_RDWR | O_NOCTTY, 0), ptn = -1;
    SC(SYS_ioctl, ptm, TIOCGPTN, &ptn);
    check("pty id reused", ptn, 0);
    check("TIOCSPGRP not our terminal", SC(SYS_ioctl, ptm, TIOCSPGRP, &c), -ENOTTY);
    SC(SYS_close, ptm, 0, 0);
    check("closed pty removed", SC(SYS_stat, "/dev/pts/0", sb, 0), -ENOENT);

    /* --- stopping and continuing --- */
    /* the child runs on after being continued, until it is killed (an
     * exit would be reported before the continue) */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        SC(SYS_kill, SC(SYS_getpid, 0, 0, 0), SIGSTOP, 0);
        spin_forever();
    }
    check("waitpid stopped", SC(SYS_waitpid, c, &st, WUNTRACED), c);
    check("stopped by SIGSTOP", st, (SIGSTOP << 8) | 0x7f);
    check("not reported twice", SC(SYS_waitpid, c, &st, WUNTRACED | WNOHANG), 0);
    check("SIGCONT", SC(SYS_kill, c, SIGCONT, 0), 0);
    check("waitpid continued", SC(SYS_waitpid, c, &st, WCONTINUED), c);
    check("continued status", st, 0xffff);
    SC(SYS_kill, c, SIGKILL, 0);
    SC(SYS_waitpid, c, &st, 0);
    check("ran on after continuing", st & 0x7f, SIGKILL);

    /* signals to a stopped process wait until it continues */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c)
        spin_forever();
    SC(SYS_kill, c, SIGSTOP, 0);
    SC(SYS_waitpid, c, &st, WUNTRACED);
    SC(SYS_kill, c, SIGTERM, 0);
    aq_spin(10000000);
    check("stopped process keeps signals pending", SC(SYS_waitpid, c, &st, WNOHANG), 0);
    SC(SYS_kill, c, SIGCONT, 0);
    SC(SYS_waitpid, c, &st, 0);
    check("delivered once continued", st & 0x7f, SIGTERM);

    /* SIGKILL ends a stopped process */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c)
        spin_forever();
    SC(SYS_kill, c, SIGSTOP, 0);
    SC(SYS_waitpid, c, &st, WUNTRACED);
    SC(SYS_kill, c, SIGKILL, 0);
    check("kill a stopped process", SC(SYS_waitpid, c, &st, 0) == c && (st & 0x7f) == SIGKILL, 1);

    /* a caught SIGTSTP doesn't stop */
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        struct aquila_sigaction tsa = {(u32) sigtstp_handler, 0, 0};
        SC(SYS_sigaction, SIGTSTP, &tsa, 0);
        spin_forever();
    }
    aq_spin(10000000);
    SC(SYS_kill, c, SIGTSTP, 0);
    SC(SYS_waitpid, c, &st, WUNTRACED);
    check("SIGTSTP handler", st, 9 << 8);

    /* the terminal's line discipline and job control, on a pty */
    check("line discipline (failed step)", aq_child_status(line_discipline) >> 8, 0);

    /* --- select and poll --- */
    int pp[2];
    SC(SYS_pipe, pp, 0, 0);
    struct fdset rs = {{1UL << pp[0], 0}}, ws = {{1UL << pp[1], 0}};
    check("select pipe writable", do_select(pp[1] + 1, 0, &ws, 0), 1);
    int t1 = now_ms();
    check("select times out", do_select(pp[0] + 1, &rs, 0, 200000), 0);
    int waited = now_ms() - t1;
    check("waited 200ms", waited >= 190 && waited < 1000, 1);
    check("nothing ready", rs.bits[0], 0);
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        struct aquila_timespec d = {0, 100000000};
        SC(SYS_nanosleep, &d, 0, 0);
        SC(SYS_write, pp[1], "w", 1);
        SC(SYS_exit, 0, 0, 0);
    }
    rs.bits[0] = 1UL << pp[0];
    t1 = now_ms();
    check("select wakes up for data", do_select(pp[0] + 1, &rs, 0, -1), 1);
    check("woke before a second", now_ms() - t1 < 1000, 1);
    check("read end set", rs.bits[0], 1UL << pp[0]);
    SC(SYS_waitpid, c, &st, 0);
    struct fdset bad = {{0, 1UL << (40 - 32)}};  /* descriptor 40 */
    check("select closed fd", do_select(41, &bad, 0, 0), -EBADF);
    fd = SC(SYS_open, "/tmp/other", O_RDONLY, 0);
    rs.bits[0] = 1UL << fd;
    check("regular file always ready", do_select(fd + 1, &rs, 0, -1), 1);
    SC(SYS_close, fd, 0, 0);

    struct pollfd pfd[3] = {{pp[0], POLLIN, 0}, {pp[1], POLLOUT, 0}, {45, POLLIN, 0}};
    check("poll", SC(SYS_poll, pfd, 3, 0), 3);
    check("poll POLLIN", pfd[0].revents, POLLIN);
    check("poll POLLOUT", pfd[1].revents, POLLOUT);
    check("poll POLLNVAL", pfd[2].revents, POLLNVAL);
    char pc;
    SC(SYS_read, pp[0], &pc, 1);
    pfd[1].fd = -1;
    t1 = now_ms();
    check("poll times out", SC(SYS_poll, pfd, 2, 150), 0);
    check("waited 150ms", now_ms() - t1 >= 140, 1);
    SC(SYS_close, pp[1], 0, 0);
    check("poll end of pipe", SC(SYS_poll, pfd, 1, -1) == 1 && (pfd[0].revents & POLLIN), 1);
    SC(SYS_close, pp[0], 0, 0);

    check("terminal read timeout (failed step)", aq_child_status(read_timeout) >> 8, 0);

    /* --- clocks and nanosleep --- */
    struct aquila_timespec ts = {0, 0}, left = {0, 0};
    check("clock_gettime realtime", SC(SYS_clock_gettime, CLOCK_REALTIME, &ts, 0), 0);
    check("realtime is recent", ts.tv_sec > 1500000000, 1);
    check("clock_gettime bad clock", SC(SYS_clock_gettime, 99, &ts, 0), -EINVAL);
    check("clock_gettime EFAULT", SC(SYS_clock_gettime, CLOCK_MONOTONIC, KADDR, 0), -EFAULT);

    int t0 = now_ms();
    ts = (struct aquila_timespec) {0, 200000000};
    check("nanosleep", SC(SYS_nanosleep, &ts, 0, 0), 0);
    int slept = now_ms() - t0;
    check("slept 200ms", slept >= 200 && slept < 1000, 1);
    ts.tv_nsec = 1000000000;
    check("nanosleep bad nsec", SC(SYS_nanosleep, &ts, 0, 0), -EINVAL);

    /* two sleepers wake up at their own times */
    t0 = now_ms();
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        struct aquila_timespec d = {0, 100000000};
        SC(SYS_nanosleep, &d, 0, 0);
        SC(SYS_exit, now_ms() - t0 < 300 ? 0 : 1, 0, 0);
    }
    ts = (struct aquila_timespec) {0, 400000000};
    SC(SYS_nanosleep, &ts, 0, 0);
    SC(SYS_waitpid, c, &st, 0);
    check("child woke first", st >> 8, 0);

    /* CPU time: our own, then a child's once waited for */
    struct aquila_tms tms;
    aq_spin(50000000);
    check("times", SC(SYS_times, &tms, 0, 0) > 0, 1);
    check("user time counted", tms.tms_utime > 0, 1);
    u32 cutime = tms.tms_cutime;
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        aq_spin(50000000);
        SC(SYS_exit, 0, 0, 0);
    }
    SC(SYS_waitpid, c, &st, 0);
    SC(SYS_times, &tms, 0, 0);
    check("child's time added", tms.tms_cutime > cutime, 1);
    check("times EFAULT", SC(SYS_times, KADDR, 0, 0), -EFAULT);

    /* interrupted by a signal: EINTR and the time left */
    sa.sa_handler = (u32) handler;
    SC(SYS_sigaction, SIGUSR1, &sa, 0);
    c = SC(SYS_fork, 0, 0, 0);
    if (!c) {
        struct aquila_timespec d = {0, 100000000};
        SC(SYS_nanosleep, &d, 0, 0);
        SC(SYS_kill, SC(SYS_getppid, 0, 0, 0), SIGUSR1, 0);
        SC(SYS_exit, 0, 0, 0);
    }
    got_sig = 0;
    ts = (struct aquila_timespec) {10, 0};
    t0 = now_ms();
    check("nanosleep interrupted", SC(SYS_nanosleep, &ts, &left, 0), -EINTR);
    check("handler ran", got_sig, SIGUSR1);
    check("woke early", now_ms() - t0 < 2000, 1);
    check("time left", left.tv_sec >= 8 && left.tv_sec <= 9, 1);
    SC(SYS_waitpid, c, &st, 0);

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

    /* --- virtual terminals --- */
    SC(SYS_mknod, "/dev/tty0", S_IFCHR | 0600, DEV(4, 0));
    for (int i = 1; i <= 6; ++i)
        SC(SYS_mknod, vt_path(i), S_IFCHR | 0600, DEV(4, i));

    int vt0 = SC(SYS_open, "/dev/tty0", O_RDWR | O_NOCTTY, 0);
    check("open /dev/tty0", vt0 >= 0, 1);

    char kbtype = 0;
    int vi = -1;
    check("KDGKBTYPE", SC(SYS_ioctl, vt0, KDGKBTYPE, &kbtype), 0);
    check("KDGKBTYPE KB_101", kbtype, KB_101);
    check("KDGETMODE kaddr", SC(SYS_ioctl, vt0, KDGETMODE, KADDR), -EFAULT);
    check("KDGETMODE", SC(SYS_ioctl, vt0, KDGETMODE, &vi), 0);
    check("KDGETMODE text", vi, KD_TEXT);
    check("KDSETMODE bad", SC(SYS_ioctl, vt0, KDSETMODE, 7), -EINVAL);
    check("KDGKBMODE", SC(SYS_ioctl, vt0, KDGKBMODE, &vi), 0);
    check("KDGKBMODE xlate", vi, K_XLATE);
    check("KDSKBMODE bad", SC(SYS_ioctl, vt0, KDSKBMODE, 9), -EINVAL);
    check("VT_GETSTATE kaddr", SC(SYS_ioctl, vt0, VT_GETSTATE, KADDR), -EFAULT);
    check("VT_GETSTATE active", vt_active(vt0), 1);
    check("VT_ACTIVATE 0", SC(SYS_ioctl, vt0, VT_ACTIVATE, 0), -ENXIO);
    check("VT_ACTIVATE 7", SC(SYS_ioctl, vt0, VT_ACTIVATE, 7), -ENXIO);

    /* a freevt terminal, in use once it is open */
    int freevt = -1, again = -1;
    check("VT_OPENQRY", SC(SYS_ioctl, vt0, VT_OPENQRY, &freevt), 0);
    check("VT_OPENQRY found one", freevt > 1 && freevt <= 6, 1);
    int tn = SC(SYS_open, vt_path(freevt), O_RDWR | O_NOCTTY, 0);
    SC(SYS_ioctl, vt0, VT_OPENQRY, &again);
    check("VT_OPENQRY skips open ones", again != freevt, 1);
    struct vt_stat vs = {0};
    SC(SYS_ioctl, vt0, VT_GETSTATE, &vs);
    check("VT_GETSTATE open", (vs.v_state >> freevt) & 1, 1);

    /* the modes of a process that is gone are undone */
    int vc = SC(SYS_fork, 0, 0, 0);
    if (!vc) {
        int f = SC(SYS_open, "/dev/tty1", O_RDWR | O_NOCTTY, 0);
        SC(SYS_ioctl, f, KDSETMODE, KD_GRAPHICS);
        SC(SYS_ioctl, f, KDSKBMODE, K_MEDIUMRAW);
        SC(SYS_exit, 0, 0, 0);
    }
    SC(SYS_waitpid, vc, &ast, 0);
    int vt1 = SC(SYS_open, "/dev/tty1", O_RDWR | O_NOCTTY, 0);
    SC(SYS_ioctl, vt1, KDGETMODE, &vi);
    check("KD_GRAPHICS left by a child", vi, KD_GRAPHICS);
    check("VT_ACTIVATE", SC(SYS_ioctl, vt0, VT_ACTIVATE, freevt), 0);
    check("VT_WAITACTIVE", SC(SYS_ioctl, vt0, VT_WAITACTIVE, freevt), 0);
    check("switched", vt_active(vt0), freevt);
    SC(SYS_ioctl, vt1, KDGETMODE, &vi);
    check("KD_TEXT once the child is gone", vi, KD_TEXT);
    SC(SYS_ioctl, vt1, KDGKBMODE, &vi);
    check("K_XLATE once the child is gone", vi, K_XLATE);

    /* the foreground terminal's own modes */
    check("KDSETMODE graphics", SC(SYS_ioctl, tn, KDSETMODE, KD_GRAPHICS), 0);
    SC(SYS_ioctl, vt0, KDGETMODE, &vi);
    check("/dev/tty0 is the foreground", vi, KD_GRAPHICS);
    check("KDSKBMODE raw", SC(SYS_ioctl, tn, KDSKBMODE, K_RAW), 0);
    SC(SYS_ioctl, tn, KDGKBMODE, &vi);
    check("KDGKBMODE raw", vi, K_RAW);

    /* VT_PROCESS: switching away waits for VT_RELDISP */
    struct aquila_sigaction vsa = {(u32) vt_handler, 0, 0};
    SC(SYS_sigaction, SIGUSR1, &vsa, 0);
    SC(SYS_sigaction, SIGUSR2, &vsa, 0);
    struct vt_mode vm = {VT_PROCESS, 0, SIGUSR1, SIGUSR2, 0}, vm2 = {0};
    check("VT_SETMODE bad", SC(SYS_ioctl, tn, VT_SETMODE, &(struct vt_mode) {5}), -EINVAL);
    check("VT_SETMODE", SC(SYS_ioctl, tn, VT_SETMODE, &vm), 0);
    SC(SYS_ioctl, tn, VT_GETMODE, &vm2);
    check("VT_GETMODE", vm2.mode == VT_PROCESS && vm2.relsig == SIGUSR1 && vm2.acqsig == SIGUSR2, 1);
    check("VT_RELDISP nothing pending", SC(SYS_ioctl, tn, VT_RELDISP, 1), -EINVAL);
    SC(SYS_ioctl, vt0, VT_ACTIVATE, 1);
    check("relsig", vt_rel, 1);
    check("not switched yet", vt_active(vt0), freevt);
    check("VT_RELDISP refuse", SC(SYS_ioctl, tn, VT_RELDISP, 0), 0);
    check("refused", vt_active(vt0), freevt);
    SC(SYS_ioctl, vt0, VT_ACTIVATE, 1);
    check("relsig again", vt_rel, 2);
    check("VT_RELDISP release", SC(SYS_ioctl, tn, VT_RELDISP, 1), 0);
    check("released", vt_active(vt0), 1);
    check("VT_RELDISP in the background", SC(SYS_ioctl, tn, VT_RELDISP, 1), -EINVAL);
    check("no acqsig yet", vt_acq, 0);
    SC(SYS_ioctl, vt0, VT_ACTIVATE, freevt);
    check("acqsig", vt_acq, 1);
    check("VT_RELDISP VT_ACKACQ", SC(SYS_ioctl, tn, VT_RELDISP, VT_ACKACQ), 0);
    vm.mode = VT_AUTO;
    SC(SYS_ioctl, tn, VT_SETMODE, &vm);
    SC(SYS_ioctl, tn, KDSKBMODE, K_XLATE);
    SC(SYS_ioctl, tn, KDSETMODE, KD_TEXT);
    SC(SYS_ioctl, vt0, VT_ACTIVATE, 1);
    check("VT_AUTO", vt_active(vt0), 1);

    /* a VT_PROCESS process that is gone doesn't hold the terminal */
    vc = SC(SYS_fork, 0, 0, 0);
    if (!vc) {
        struct vt_mode cm = {VT_PROCESS, 0, SIGUSR1, SIGUSR2, 0};
        SC(SYS_ioctl, vt1, VT_SETMODE, &cm);
        SC(SYS_exit, 0, 0, 0);
    }
    SC(SYS_waitpid, vc, &ast, 0);
    SC(SYS_ioctl, vt0, VT_ACTIVATE, freevt);
    check("VT_PROCESS of a child that is gone", vt_active(vt0), freevt);
    SC(SYS_ioctl, vt0, VT_ACTIVATE, 1);

    SC(SYS_close, tn, 0, 0);
    SC(SYS_ioctl, vt0, VT_OPENQRY, &again);
    check("VT_OPENQRY once closed", again, freevt);
    SC(SYS_close, vt1, 0, 0);
    SC(SYS_close, vt0, 0, 0);

    aq_end();
}
