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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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

    fprintf(out, "aqtest: done, fails=%d\n", fails);

    for (;;)
        getpid();
}
