/*
 * C library on the kernel
 *
 * Built with i686-aquila-gcc against newlib (unlike the other tests),
 * checks that what programs see through libc matches the kernel: errno
 * values, signal numbers, struct stat, and a few library functions that
 * depend on the port (stdio, getline, opendir, pipes, fork/wait).
 *
 * Reports in the aqtest format on the serial port.
 */

#include <arpa/inet.h>
#include <crypt.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <shadow.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static FILE *out;
static int fails;

static void check(const char *name, long got, long want)
{
    fprintf(out, "%s %s got=%ld want=%ld\n", got == want ? "PASS" : "FAIL", name, got, want);
    if (got != want)
        ++fails;
}

static volatile sig_atomic_t got_signal;
static sigjmp_buf fault_jmp;
static void *volatile fault_addr;

/* SA_SIGINFO: the faulting address, then back to the test */
static void fault_handler(int sig, siginfo_t *info, void *ctx)
{
    fault_addr = info->si_addr;
    siglongjmp(fault_jmp, sig);
}

static void handler(int sig)
{
    got_signal = sig;
}

int main(void)
{
    struct { char *dev, *opt; } no_args = {0, 0};

    mount("devfs", "/dev", 0, &no_args);
    mknod("/dev/ttyS0", S_IFCHR | 0666, (4 << 8) | 64);
    out = fopen("/dev/ttyS0", "w");
    setvbuf(out, NULL, _IONBF, 0);
    fprintf(out, "aqtest: libc start\n");

    /* errno values as seen through libc */
    errno = 0;
    check("open missing -> ENOENT", open("/nonexistent", O_RDONLY) == -1 ? errno : 0, ENOENT);
    errno = 0;
    check("rmdir read-only fs -> EROFS", rmdir("/tmp") == -1 ? errno : 0, EROFS);
    errno = 0;
    check("close bad fd -> EBADF", close(99) == -1 ? errno : 0, EBADF);

    /* signal numbers: the kernel must agree on SIGUSR1 (30 in newlib) */
    signal(SIGUSR1, handler);
    check("kill(self, SIGUSR1)", kill(getpid(), SIGUSR1), 0);
    check("SIGUSR1 handler ran", got_signal, SIGUSR1);

    errno = 0;
    check("kill bad signal -> EINVAL", kill(getpid(), 99) == -1 ? errno : 0, EINVAL);

    /* struct stat layout and times */
    mount("tmpfs", "/tmp", 0, NULL);

    FILE *f = fopen("/tmp/file", "w");
    check("fopen for writing", f != NULL, 1);
    fputs("first line\nsecond line\n", f);
    fclose(f);

    struct stat st;
    check("stat", stat("/tmp/file", &st), 0);
    check("st_size", st.st_size, 23);
    check("S_ISREG", S_ISREG(st.st_mode), 1);
    check("st_mtime is recent", st.st_mtime > 1500000000, 1);

    /* stdio reading and getline */
    f = fopen("/tmp/file", "r");
    char *line = NULL;
    size_t cap = 0;
    check("getline", getline(&line, &cap, f), 11);
    check("getline contents", strcmp(line, "first line\n"), 0);
    free(line);
    fclose(f);

    /* directories */
    DIR *dir = opendir("/tmp");
    int found = 0;
    struct dirent *ent;
    while (dir && (ent = readdir(dir)))
        if (!strcmp(ent->d_name, "file"))
            found = 1;
    if (dir)
        closedir(dir);
    check("readdir finds the file", found, 1);

    /* pipes, fork and wait */
    int p[2];
    check("pipe", pipe(p), 0);

    pid_t pid = fork();
    if (pid == 0) {
        close(p[0]);
        write(p[1], "hello", 5);
        _exit(7);
    }

    close(p[1]);
    char buf[16] = {0};
    ssize_t n, total = 0;
    while ((n = read(p[0], buf + total, sizeof(buf) - 1 - total)) > 0)
        total += n;
    close(p[0]);

    int status;
    check("waitpid", waitpid(pid, &status, 0), pid);
    check("child exit status", WEXITSTATUS(status), 7);
    check("pipe data then EOF", total, 5);
    check("pipe contents", strcmp(buf, "hello"), 0);

    /* misc */
    struct utsname uts;
    check("uname", uname(&uts), 0);
    check("sysname", strcmp(uts.sysname, "AquilaOS"), 0);
    check("time", time(NULL) > 1500000000, 1);

    /* crypt: vectors from the SHA-512 crypt specification */
    const char *h = crypt("Hello world!", "$6$saltstring");
    check("crypt default rounds", h && !strcmp(h, "$6$saltstring$svn8UoSVapNtMuq1ukKS4tPQd8iKwSMHWjl/"
        "O817G3uBnIFNjnQJuesI68u4OTLiBFdcbYEdFCoEOfaS35inz1"), 1);
    h = crypt("the minimum number is still observed", "$6$rounds=10$roundstoolow");
    check("crypt minimum rounds", h && !strcmp(h, "$6$rounds=1000$roundstoolow$kUMsbe306n21p9R.FRkW3IGn."
        "S9NPN0x50YhH1xhLsPuWGsUSklZt58jaTfF4ZEQpyUNGc0dqbpBYYBaHHrsX."), 1);
    errno = 0;
    check("crypt unsupported -> EINVAL", crypt("x", "$1$salt") == NULL ? errno : 0, EINVAL);

    /* passwd and shadow (etc/ comes from tests/libc/rootfs) */
    struct passwd *pw = getpwnam("tester");
    check("getpwnam", pw != NULL, 1);
    check("pw_uid", pw ? (long) pw->pw_uid : -1, 1000);

    struct spwd *sp = getspnam("tester");
    check("getspnam", sp != NULL, 1);
    check("sp_lstchg", sp ? sp->sp_lstchg : -2, 19000);
    check("sp_inact empty", sp ? sp->sp_inact : -2, -1);
    check("password verifies", sp && !strcmp(crypt("tester-password", sp->sp_pwdp), sp->sp_pwdp), 1);
    check("wrong password fails", sp && strcmp(crypt("tester-passwore", sp->sp_pwdp), sp->sp_pwdp) != 0, 1);
    check("getspnam unknown", getspnam("nobody") == NULL, 1);

    /* dropping privileges */
    pid = fork();
    if (pid == 0)
        _exit(setgid(100) || setuid(1000) || getuid() != 1000 || setuid(0) == 0 ||
              getspnam("tester") != NULL);
    check("privilege drop", waitpid(pid, &status, 0) == pid && WEXITSTATUS(status) == 0, 1);

    /* truncate (tmpfs mounted above) */
    f = fopen("/tmp/trunc", "w");
    fputs("0123456789", f);
    fclose(f);
    check("truncate", truncate("/tmp/trunc", 4), 0);
    check("truncated size", stat("/tmp/trunc", &st) == 0 ? (long) st.st_size : -1, 4);
    int tfd = open("/tmp/trunc", O_RDWR);
    check("ftruncate grow", ftruncate(tfd, 8), 0);
    char tbuf[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    check("read grown file", read(tfd, tbuf, 8), 8);
    check("grown part zeroed", memcmp(tbuf, "0123\0\0\0\0", 8), 0);
    close(tfd);

    /* wait3, getppid */
    pid_t self = getpid();
    pid = fork();
    if (pid == 0)
        _exit(getppid() == self ? 3 : 4);
    check("wait3", wait3(&status, 0, NULL), pid);
    check("wait3 status", WEXITSTATUS(status), 3);

    /* killpg: the child makes its own group, then waits to be killed */
    pipe(p);
    pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        write(p[1], "x", 1);
        for (;;)
            getpid();
    }
    read(p[0], buf, 1);
    close(p[0]);
    close(p[1]);
    check("killpg", killpg(pid, SIGTERM), 0);
    check("killpg wait", waitpid(pid, &status, 0), pid);
    check("killed by SIGTERM", WIFSIGNALED(status) ? WTERMSIG(status) : 0, SIGTERM);

    /* signal mask: a blocked signal stays pending until sigsuspend */
    sigset_t block, old, none;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR2);
    sigemptyset(&none);
    signal(SIGUSR2, handler);
    got_signal = 0;
    check("sigprocmask block", sigprocmask(SIG_BLOCK, &block, &old), 0);
    kill(getpid(), SIGUSR2);
    check("blocked signal not delivered", got_signal, 0);
    errno = 0;
    check("sigsuspend -> EINTR", sigsuspend(&none) == -1 ? errno : 0, EINTR);
    check("delivered in sigsuspend", got_signal, SIGUSR2);
    sigprocmask(SIG_SETMASK, &old, NULL);

    /* byte order */
    check("htonl", htonl(0x01020304) == 0x04030201, 1);
    check("ntohs", ntohs(htons(0xabcd)), 0xabcd);

    /* files through the *at() calls and the functions built on them */
    check("mkdir", mkdir("/tmp/dir", 0755), 0);
    int dfd = open("/tmp/dir", O_RDONLY | O_DIRECTORY);
    check("open directory", dfd >= 0, 1);
    tfd = openat(dfd, "f", O_CREAT | O_WRONLY, 0644);
    check("openat", tfd >= 0, 1);
    write(tfd, "data", 4);
    close(tfd);
    check("fstatat", fstatat(dfd, "f", &st, 0) == 0 && st.st_size == 4, 1);
    check("symlink", symlink("dir/f", "/tmp/link"), 0);
    char lbuf[32] = {0};
    check("readlink", readlink("/tmp/link", lbuf, sizeof(lbuf)), 5);
    check("readlink contents", strcmp(lbuf, "dir/f"), 0);
    char *rp = realpath("/tmp/./link", NULL);
    check("realpath through a link", rp && !strcmp(rp, "/tmp/dir/f"), 1);
    free(rp);
    symlink("/tmp/dir", "/tmp/dl");
    check("realpath .. after a link", realpath("/tmp/dl/../dir/f", lbuf) && !strcmp(lbuf, "/tmp/dir/f"), 1);
    chdir("/tmp/dir");
    check("realpath relative", realpath("../dl/./f", lbuf) && !strcmp(lbuf, "/tmp/dir/f"), 1);
    chdir("/");
    errno = 0;
    check("realpath file as directory", realpath("/tmp/dir/f/x", lbuf) == NULL ? errno : 0, ENOTDIR);
    errno = 0;
    check("realpath missing", realpath("/tmp/nothing/x", lbuf) == NULL ? errno : 0, ENOENT);
    check("rename a directory", rename("/tmp/dir", "/tmp/dir2"), 0);
    check("renamed", stat("/tmp/dir2/f", &st), 0);
    DIR *dp = fdopendir(openat(AT_FDCWD, "/tmp/dir2", O_RDONLY | O_DIRECTORY));
    int entries = 0;
    struct dirent *de;
    while (dp && (de = readdir(dp)))
        entries += !strcmp(de->d_name, "f");
    check("fdopendir", entries, 1);
    if (dp)
        closedir(dp);
    errno = 0;
    check("rmdir not empty", rmdir("/tmp/dir2") == -1 ? errno : 0, ENOTEMPTY);
    check("unlinkat", unlinkat(AT_FDCWD, "/tmp/dir2/f", 0), 0);
    check("rmdir", rmdir("/tmp/dir2"), 0);
    close(dfd);
    check("chmod", chmod("/tmp/trunc", 0600) == 0 && stat("/tmp/trunc", &st) == 0 && (st.st_mode & 0777) == 0600, 1);
    struct timespec times[2] = {{1000, 0}, {2000, 0}};
    check("utimensat", utimensat(AT_FDCWD, "/tmp/trunc", times, 0), 0);
    check("mtime set", stat("/tmp/trunc", &st) == 0 && st.st_mtime == 2000, 1);

    /* /etc/group (tests/libc/rootfs) */
    struct group *gr = getgrnam("users");
    check("getgrnam", gr && gr->gr_gid == 100, 1);
    check("group members", gr && gr->gr_mem[0] && !strcmp(gr->gr_mem[1], "other") && !gr->gr_mem[2], 1);
    gr = getgrgid(0);
    check("getgrgid", gr && !strcmp(gr->gr_name, "root"), 1);
    check("getgrnam unknown", getgrnam("nobody") == NULL, 1);

    /* host name */
    char host[64];
    check("sethostname", sethostname("testhost", 8), 0);
    check("gethostname", gethostname(host, sizeof(host)) == 0 && !strcmp(host, "testhost"), 1);

    /* sleeping and clocks */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    check("usleep", usleep(100000), 0);
    check("sleep", sleep(1), 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    check("slept 1.1s", ms >= 1100 && ms < 2000, 1);

    /* terminals: the serial port is one, a pipe isn't */
    check("isatty tty", isatty(fileno(out)), 1);
    char *tty = ttyname(fileno(out));
    check("ttyname", tty && !strcmp(tty, "/dev/ttyS0"), 1);
    pipe(p);
    errno = 0;
    check("isatty pipe", isatty(p[0]) == 0 && errno == ENOTTY, 1);
    errno = 0;
    check("lseek pipe -> ESPIPE", lseek(p[0], 0, SEEK_CUR) == -1 ? errno : 0, ESPIPE);

    /* writing to a pipe without readers */
    close(p[0]);
    signal(SIGPIPE, SIG_IGN);
    errno = 0;
    check("EPIPE", write(p[1], "x", 1) == -1 ? errno : 0, EPIPE);
    signal(SIGPIPE, SIG_DFL);
    pid = fork();
    if (pid == 0) {
        write(p[1], "x", 1);
        _exit(0);
    }
    close(p[1]);
    check("SIGPIPE", waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) ? WTERMSIG(status) : 0, SIGPIPE);

    /* select and poll through libc */
    int sp2[2];
    pipe(sp2);
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sp2[0], &rfds);
    struct timeval tv = {0, 100000};
    check("select timeout", select(sp2[0] + 1, &rfds, NULL, NULL, &tv), 0);
    write(sp2[1], "s", 1);
    FD_ZERO(&rfds);
    FD_SET(sp2[0], &rfds);
    check("select readable", select(sp2[0] + 1, &rfds, NULL, NULL, NULL), 1);
    check("FD_ISSET", FD_ISSET(sp2[0], &rfds) != 0, 1);
    struct pollfd pfd = {sp2[0], POLLIN, 0};
    check("poll", poll(&pfd, 1, -1) == 1 && pfd.revents == POLLIN, 1);
    close(sp2[0]);
    close(sp2[1]);

    /* siginfo and sigsetjmp: a fault reports its address */
    struct sigaction fsa = {0};
    fsa.sa_sigaction = fault_handler;
    fsa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &fsa, NULL);
    int jumped = sigsetjmp(fault_jmp, 1);
    if (!jumped)
        *(volatile int *) 0x1234 = 1;
    check("SIGSEGV handler ran", jumped, SIGSEGV);
    check("si_addr", (long) fault_addr, 0x1234);
    signal(SIGSEGV, SIG_DFL);

    /* pselect: the signal blocked outside is let through while waiting */
    sigset_t blk, empty, cur;
    sigemptyset(&blk);
    sigaddset(&blk, SIGUSR1);
    sigemptyset(&empty);
    sigprocmask(SIG_BLOCK, &blk, NULL);
    signal(SIGUSR1, handler);
    got_signal = 0;
    pipe(sp2);
    pid = fork();
    if (pid == 0) {
        usleep(100000);
        kill(getppid(), SIGUSR1);
        _exit(0);
    }
    FD_ZERO(&rfds);
    FD_SET(sp2[0], &rfds);
    struct timespec pts = {2, 0};
    errno = 0;
    check("pselect -> EINTR", pselect(sp2[0] + 1, &rfds, NULL, NULL, &pts, &empty) == -1 ? errno : 0, EINTR);
    check("signal delivered in pselect", got_signal, SIGUSR1);
    sigprocmask(SIG_SETMASK, NULL, &cur);
    check("mask restored", sigismember(&cur, SIGUSR1), 1);
    sigprocmask(SIG_UNBLOCK, &blk, NULL);
    waitpid(pid, &status, 0);
    close(sp2[0]);
    close(sp2[1]);

    /* descriptors */
    tfd = open("/tmp/trunc", O_RDONLY);
    errno = 0;
    check("write read-only -> EBADF", write(tfd, "x", 1) == -1 ? errno : 0, EBADF);
    check("fsync", fsync(tfd), 0);
    close(tfd);
    errno = 0;
    check("fsync closed -> EBADF", fsync(tfd) == -1 ? errno : 0, EBADF);

    /* the serial port is init's controlling terminal */
    check("tcgetsid", tcgetsid(fileno(out)), getpid());
    check("tcflush", tcflush(fileno(out), TCIFLUSH), 0);

    /* C99 printf formats */
    char fmt[32];
    snprintf(fmt, sizeof(fmt), "%zu %jd %hhu", (size_t) 42, (intmax_t) -7, (unsigned char) 300);
    check("printf %zu %jd %hhu", strcmp(fmt, "42 -7 44"), 0);

    fprintf(out, "aqtest: done, fails=%d\n", fails);

    for (;;)
        getpid();
}
