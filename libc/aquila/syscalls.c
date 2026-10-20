/*
 * AquilaOS system call wrappers
 *
 * Built with MISSING_SYSCALL_NAMES, so newlib's reentrant layer calls
 * these directly (read, write, ...) instead of _read, _write, ...
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <aquila/syscall.h>
#include <aquila/time.h>

#include "syscall.h"

void _exit(int status)
{
    __syscall(SYS_exit, status, 0, 0);

    /* not reached */
    for (;;);
}

int close(int fildes)
{
    return SYSCALL(SYS_close, fildes, 0, 0);
}

/* newlib's posix/ provides execve() and the exec* family on top of this */
int _execve(const char *path, char *const argv[], char *const envp[])
{
    return SYSCALL(SYS_execve, path, argv, envp);
}

pid_t fork(void)
{
    return SYSCALL(SYS_fork, 0, 0, 0);
}

pid_t vfork(void)
{
    return fork();
}

int fstat(int fildes, struct stat *buf)
{
    return SYSCALL(SYS_fstat, fildes, buf, 0);
}

int stat(const char *path, struct stat *buf)
{
    return SYSCALL(SYS_stat, path, buf, 0);
}

int lstat(const char *path, struct stat *buf)
{
    return SYSCALL(SYS_lstat, path, buf, 0);
}

pid_t getpid(void)
{
    return SYSCALL(SYS_getpid, 0, 0, 0);
}

pid_t getppid(void)
{
    return SYSCALL(SYS_getppid, 0, 0, 0);
}

/* newlib's posix/ provides isatty() on top of this: a terminal is what
 * answers terminal ioctls */
int _isatty(int fildes)
{
    struct termios tios;

    if (tcgetattr(fildes, &tios)) {
        if (errno != EBADF)
            errno = ENOTTY;
        return 0;
    }

    return 1;
}

int kill(pid_t pid, int sig)
{
    return SYSCALL(SYS_kill, pid, sig, 0);
}

int killpg(pid_t pgrp, int sig)
{
    if (pgrp < 0) {
        errno = EINVAL;
        return -1;
    }

    return kill(-pgrp, sig);
}

int link(const char *old, const char *new)
{
    return SYSCALL(SYS_link, old, new, 0);
}

int unlink(const char *path)
{
    return SYSCALL(SYS_unlink, path, 0, 0);
}

off_t lseek(int fildes, off_t offset, int whence)
{
    return SYSCALL(SYS_lseek, fildes, offset, whence);
}

int open(const char *path, int oflags, ...)
{
    mode_t mode = 0;

    if (oflags & O_CREAT) {
        va_list ap;
        va_start(ap, oflags);
        mode = va_arg(ap, int);
        va_end(ap);
    }

    return SYSCALL(SYS_open, path, oflags, mode);
}

ssize_t read(int fildes, void *buf, size_t nbytes)
{
    return SYSCALL(SYS_read, fildes, buf, nbytes);
}

ssize_t write(int fildes, const void *buf, size_t nbytes)
{
    return SYSCALL(SYS_write, fildes, buf, nbytes);
}

void *sbrk(ptrdiff_t incr)
{
    /* the kernel returns the old break, or (void *) -1 */
    void *ret = (void *) __syscall(SYS_sbrk, incr, 0, 0);

    if (ret == (void *) -1)
        errno = ENOMEM;

    return ret;
}

clock_t times(struct tms *buf)
{
    /* the result is a time, any value is valid */
    long ret = __syscall(SYS_times, (long) buf, 0, 0);

    if (ret < 0 && ret >= -4095) {
        errno = -ret;
        return (clock_t) -1;
    }

    return ret;
}

pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
    return SYSCALL(SYS_waitpid, pid, stat_loc, options);
}

pid_t wait(int *stat_loc)
{
    return waitpid(-1, stat_loc, 0);
}

/* BSD wait3(), the kernel doesn't track resource usage */
pid_t wait3(int *stat_loc, int options, struct rusage *rusage)
{
    if (rusage)
        memset(rusage, 0, sizeof(*rusage));

    return waitpid(-1, stat_loc, options);
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    return SYSCALL(SYS_sigaction, sig, act, oact);
}

_sig_func_ptr signal(int sig, _sig_func_ptr handler)
{
    struct sigaction act = {0}, oact = {0};

    act.sa_handler = handler;

    if (sigaction(sig, &act, &oact) == -1)
        return SIG_ERR;

    return oact.sa_handler;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    return SYSCALL(SYS_sigmask, how, set, oldset);
}

int sigsuspend(const sigset_t *mask)
{
    /* always fails with EINTR once a signal handler ran */
    return SYSCALL(SYS_sigsuspend, mask, 0, 0);
}

int ioctl(int fildes, int request, ...)
{
    va_list ap;
    va_start(ap, request);
    void *argp = va_arg(ap, void *);
    va_end(ap);

    return SYSCALL(SYS_ioctl, fildes, request, argp);
}

int mount(const char *type, const char *dir, int flags, void *data)
{
    struct aquila_mount_args args = {type, dir, flags, data};
    return SYSCALL(SYS_mount, &args, 0, 0);
}

int mkdir(const char *path, mode_t mode)
{
    return SYSCALL(SYS_mkdir, path, mode, 0);
}

int rmdir(const char *path)
{
    return SYSCALL(SYS_rmdir, path, 0, 0);
}

int gethostname(char *name, size_t len)
{
    struct utsname uts;

    if (uname(&uts))
        return -1;

    if (strlen(uts.nodename) >= len) {
        errno = ENAMETOOLONG;
        return -1;
    }

    strcpy(name, uts.nodename);
    return 0;
}

int sethostname(const char *name, size_t len)
{
    return SYSCALL(SYS_sethostname, name, len, 0);
}

int uname(struct utsname *name)
{
    return SYSCALL(SYS_uname, name, 0, 0);
}

int pipe(int fd[2])
{
    return SYSCALL(SYS_pipe, fd, 0, 0);
}

int fcntl(int fildes, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);

    return SYSCALL(SYS_fcntl, fildes, cmd, arg);
}

int dup(int fildes)
{
    return fcntl(fildes, F_DUPFD, 0);
}

int dup2(int fildes, int fildes2)
{
    if (fildes == fildes2)
        return fcntl(fildes, F_GETFD) == -1 ? -1 : fildes2;

    /* F_DUPFD returns the lowest free descriptor >= fildes2 */
    close(fildes2);

    int fd = fcntl(fildes, F_DUPFD, fildes2);

    if (fd != -1 && fd != fildes2) {
        close(fd);
        errno = EBUSY;
        return -1;
    }

    return fd;
}

int fchdir(int fildes)
{
    return SYSCALL(SYS_fchdir, fildes, 0, 0);
}

int chdir(const char *path)
{
    return SYSCALL(SYS_chdir, path, 0, 0);
}

char *getcwd(char *buf, size_t size)
{
    if (SYSCALL(SYS_getcwd, buf, size, 0) == -1)
        return NULL;

    return buf;
}

int setpgid(pid_t pid, pid_t pgid)
{
    return SYSCALL(SYS_setpgid, pid, pgid, 0);
}

pid_t setsid(void)
{
    return SYSCALL(SYS_setsid, 0, 0, 0);
}

pid_t getpgrp(void)
{
    return SYSCALL(SYS_getpgrp, 0, 0, 0);
}

int mknod(const char *path, mode_t mode, dev_t dev)
{
    return SYSCALL(SYS_mknod, path, mode, dev);
}

uid_t getuid(void)
{
    return __syscall(SYS_getuid, 0, 0, 0);
}

uid_t geteuid(void)
{
    return getuid();
}

gid_t getgid(void)
{
    return __syscall(SYS_getgid, 0, 0, 0);
}

gid_t getegid(void)
{
    return getgid();
}

int getgroups(int size, gid_t list[])
{
    /* no supplementary groups */
    if (size < 0) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

/* there are no separate effective ids */
int setuid(uid_t uid)
{
    return SYSCALL(SYS_setuid, uid, 0, 0);
}

int seteuid(uid_t uid)
{
    return setuid(uid);
}

int setgid(gid_t gid)
{
    return SYSCALL(SYS_setgid, gid, 0, 0);
}

int setegid(gid_t gid)
{
    return setgid(gid);
}

void *mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
    struct aquila_mmap_args args = {addr, len, prot, flags, fildes, off};
    void *ret;

    if (SYSCALL(SYS_mmap, &args, &ret, 0) == -1)
        return MAP_FAILED;

    return ret;
}

int munmap(void *addr, size_t len)
{
    return SYSCALL(SYS_munmap, addr, len, 0);
}

int socket(int domain, int type, int protocol)
{
    return SYSCALL(SYS_socket, domain, type, protocol);
}

int accept(int fd, struct sockaddr *addr, socklen_t *len)
{
    return SYSCALL(SYS_accept, fd, addr, len);
}

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    return SYSCALL(SYS_bind, fd, addr, len);
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    return SYSCALL(SYS_connect, fd, addr, len);
}

int listen(int fd, int backlog)
{
    return SYSCALL(SYS_listen, fd, backlog, 0);
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
    struct aquila_socket_io_args args = {fd, (void *) buf, len, flags};
    return SYSCALL(SYS_send, &args, 0, 0);
}

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
    struct aquila_socket_io_args args = {fd, buf, len, flags};
    return SYSCALL(SYS_recv, &args, 0, 0);
}

mode_t umask(mode_t mask)
{
    return __syscall(SYS_umask, mask, 0, 0);
}

int chmod(const char *path, mode_t mode)
{
    return SYSCALL(SYS_chmod, path, mode, 0);
}

int fchmod(int fildes, mode_t mode)
{
    return SYSCALL(SYS_fchmod, fildes, mode, 0);
}

int chown(const char *path, uid_t owner, gid_t group)
{
    return SYSCALL(SYS_chown, path, owner, group);
}

int fchown(int fildes, uid_t owner, gid_t group)
{
    return SYSCALL(SYS_fchown, fildes, owner, group);
}

int lchown(const char *path, uid_t owner, gid_t group)
{
    return SYSCALL(SYS_lchown, path, owner, group);
}

int access(const char *path, int mode)
{
    return SYSCALL(SYS_access, path, mode, 0);
}

long sysconf(int name)
{
    switch (name) {
        case _SC_ARG_MAX:           return ARG_MAX;     /* the kernel takes 128K */
        case _SC_CHILD_MAX:         return CHILD_MAX;
        case _SC_CLK_TCK:           return AQUILA_CLK_TCK;
        case _SC_NGROUPS_MAX:       return NGROUPS_MAX;
        case _SC_OPEN_MAX:          return 64;          /* FDS_COUNT */
        case _SC_PAGESIZE:          return 4096;
        case _SC_NPROCESSORS_CONF:
        case _SC_NPROCESSORS_ONLN:  return 1;
        case _SC_HOST_NAME_MAX:     return 63;          /* utsname nodename */
        case _SC_LOGIN_NAME_MAX:    return 32;
        case _SC_TTY_NAME_MAX:      return 32;
        case _SC_SYMLOOP_MAX:       return SYMLOOP_MAX;
        case _SC_LINE_MAX:          return LINE_MAX;
        case _SC_RE_DUP_MAX:        return RE_DUP_MAX;
        case _SC_EXPR_NEST_MAX:     return EXPR_NEST_MAX;
        case _SC_BC_BASE_MAX:       return BC_BASE_MAX;
        case _SC_BC_DIM_MAX:        return BC_DIM_MAX;
        case _SC_BC_SCALE_MAX:      return BC_SCALE_MAX;
        case _SC_BC_STRING_MAX:     return BC_STRING_MAX;
        case _SC_COLL_WEIGHTS_MAX:  return COLL_WEIGHTS_MAX;
        case _SC_IOV_MAX:           return IOV_MAX;
        case _SC_GETPW_R_SIZE_MAX:
        case _SC_GETGR_R_SIZE_MAX:  return 1024;
        case _SC_JOB_CONTROL:       return 1;
        case _SC_MONOTONIC_CLOCK:   return _POSIX_MONOTONIC_CLOCK;
    }

    errno = EINVAL;
    return -1;
}

/* the same for every file */
static long file_limit(int name)
{
    switch (name) {
        case _PC_LINK_MAX:          return LINK_MAX;
        case _PC_MAX_CANON:         return MAX_CANON;
        case _PC_MAX_INPUT:         return MAX_INPUT;
        case _PC_NAME_MAX:          return MAXNAMELEN - 1;
        case _PC_PATH_MAX:          return PATH_MAX;
        case _PC_PIPE_BUF:          return PIPE_BUF;
        case _PC_SYMLINK_MAX:       return 1023;        /* lookup reads 1K */
        case _PC_CHOWN_RESTRICTED:  return 1;
        case _PC_NO_TRUNC:          return 1;
        case _PC_VDISABLE:          return 0;
    }

    errno = EINVAL;
    return -1;
}

long pathconf(const char *path, int name)
{
    struct stat st;

    if (stat(path, &st))
        return -1;

    return file_limit(name);
}

long fpathconf(int fildes, int name)
{
    struct stat st;

    if (fstat(fildes, &st))
        return -1;

    return file_limit(name);
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return SYSCALL(SYS_poll, fds, nfds, timeout);
}

int gettimeofday(struct timeval *__restrict tv, void *__restrict tz)
{
    return SYSCALL(SYS_gettimeofday, tv, tz, 0);
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
        struct timeval *timeout)
{
    struct aquila_select_args args = {nfds, readfds, writefds, exceptfds, timeout};
    return SYSCALL(SYS_select, &args, 0, 0);
}

unsigned alarm(unsigned seconds)
{
    /* not implemented by the kernel, no alarm was pending */
    return 0;
}

void sync(void)
{
}

int issetugid(void)
{
    /* no set-user-ID or set-group-ID support */
    return 0;
}

char *getlogin(void)
{
    return getenv("LOGNAME");
}

int getentropy(void *buf, size_t len)
{
    /* no kernel random source yet */
    errno = ENOSYS;
    return -1;
}

int ftruncate(int fildes, off_t len)
{
    return SYSCALL(SYS_ftruncate, fildes, len, 0);
}

int truncate(const char *path, off_t len)
{
    int fd = open(path, O_WRONLY);

    if (fd == -1)
        return -1;

    int ret = ftruncate(fd, len);
    int err = errno;

    close(fd);
    errno = err;

    return ret;
}

/* POSIX names for newlib's line reading functions */
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream)
{
    return __getdelim(lineptr, n, delim, stream);
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
    return __getline(lineptr, n, stream);
}
