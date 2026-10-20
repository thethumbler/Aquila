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
#include <pwd.h>
#include <shadow.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
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
    check("rmdir -> ENOSYS", rmdir("/tmp") == -1 ? errno : 0, ENOSYS);
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

    fprintf(out, "aqtest: done, fails=%d\n", fails);

    for (;;)
        getpid();
}
