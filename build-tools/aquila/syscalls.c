#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/times.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
 
#define SYS_EXIT    1
#define SYS_CLOSE	2
#define SYS_EXECVE	3
#define SYS_FORK	4	
#define SYS_FSTAT   5
#define SYS_GETPID  6
#define SYS_ISATTY  7
#define SYS_KILL    8 
#define SYS_LSEEK   10
#define SYS_OPEN	11
#define SYS_READ	12
#define SYS_SBRK	13
#define SYS_WAITPID 17
#define SYS_WRITE	18
#define SYS_IOCTL	19
#define SYS_SIGACT  20 
#define SYS_READDIR 21 
#define SYS_MOUNT   22 
#define SYS_MKDIR   23
#define SYS_UNAME   24
#define SYS_PIPE    25
#define SYS_FCNTL   26
#define SYS_CHDIR   27
#define SYS_GETCWD  28
#define SYS_SETPGID 32
#define SYS_MKNOD   33
#define SYS_LSTAT   34

#define SYSCALL3(ret, v, arg1, arg2, arg3) \
	asm volatile("int $0x80;":"=a"(ret):"a"(v), "b"(arg1), "c"(arg2), "d"(arg3));
#define SYSCALL2(ret, v, arg1, arg2) \
	asm volatile("int $0x80;":"=a"(ret):"a"(v), "b"(arg1), "c"(arg2));
#define SYSCALL1(ret, v, arg1) \
	asm volatile("int $0x80;":"=a"(ret):"a"(v), "b"(arg1));
#define SYSCALL0(ret, v) \
	asm volatile("int $0x80;":"=a"(ret):"a"(v));

__attribute__((noreturn)) void _exit(int status)
{
    /* Does not return */
    SYSCALL1((int){0}, SYS_EXIT, status);
}

int close(int fildes)
{
    int ret;
	SYSCALL1(ret, SYS_CLOSE, fildes);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

char **environ; /* pointer to array of char * strings that define the current environment variables */

int _execve(const char *path, char *const argv[], char *const envp[])
{
    int ret;
	SYSCALL3(ret, SYS_EXECVE, path, argv, envp);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

pid_t fork(void)
{
    pid_t ret;
	SYSCALL0(ret, SYS_FORK);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}
    
int fstat(int fildes, struct stat *buf)
{
    int ret;
    SYSCALL2(ret, SYS_FSTAT, fildes, buf);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

pid_t getpid(void)
{
    pid_t ret;
    SYSCALL0(ret, SYS_GETPID);
    return ret;
}

int isatty(int fildes)
{
    int ret;
	SYSCALL1(ret, SYS_ISATTY, fildes);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

int kill(pid_t pid, int sig)
{
    int ret;
	SYSCALL2(ret, SYS_KILL, pid, sig);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

int link(char *old, char *new)
{
    return 0;
}

off_t lseek(int fildes, off_t offset, int whence)
{
    off_t ret;
    SYSCALL3(ret, SYS_LSEEK, fildes, offset, whence);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

int open(const char *path, int oflags, ...)
{
	int ret;
    SYSCALL2(ret, SYS_OPEN, path, oflags);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

ssize_t read(int fildes, void *buf, size_t nbytes)
{
	int ret;
    SYSCALL3(ret, SYS_READ, fildes, buf, nbytes);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

void *sbrk(ptrdiff_t incr)
{
    void *ret;
	SYSCALL1(ret, SYS_SBRK, incr);

    /* TODO */
    return ret;
}

int stat(const char *file, struct stat *st)
{
    return 0;
}

clock_t times(struct tms *buf)
{
    
}

int unlink(char *name)
{
    return 0;
}

pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
    pid_t ret;
    SYSCALL3(ret, SYS_WAITPID, pid, stat_loc, options);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

pid_t wait(int *stat_loc)
{
    return waitpid(-1, stat_loc, 0);
}

ssize_t write(int fildes, const void *buf, size_t nbytes)
{
    ssize_t ret;
	SYSCALL3(ret, SYS_WRITE, fildes, buf, nbytes);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

int gettimeofday(struct timeval *p, void *z)
{
    return 0;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    int ret;
	SYSCALL3(ret, SYS_SIGACT, sig, act, oact);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

typedef void (*sighandler_t)(int);
sighandler_t signal(int sig, sighandler_t handler)
{
    struct sigaction act  = {0}, oact = {0};

    act.sa_handler = handler;
    int ret = sigaction(sig, &act, &oact);

    if (ret != -1) {
        return oact.sa_handler;
    }

    return SIG_ERR;
}

int ioctl(int fildes, int request, void *argp)
{
    int ret;
	SYSCALL3(ret, SYS_IOCTL, fildes, request, argp);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return ret;
}

DIR *opendir(const char *dirname)
{
    int fd = open(dirname, O_RDONLY);

    if (fd == -1)
        return NULL;

    DIR *dir = malloc(sizeof(DIR));
    dir->fd = fd;

    return dir;
}

struct dirent *readdir(DIR *dir)
{
    static struct dirent dirent;
    int ret;
    SYSCALL2(ret, SYS_READDIR, dir->fd, &dirent);

    if (ret <= 0) {
        errno = -ret;
        memset(&dirent, 0, sizeof(dirent));
        return NULL;
    }

    return &dirent;
}

int closedir(DIR *dir)
{
    close(dir->fd);
    free(dir);
}

int mount(const char *type, const char *dir, int flags, void *data)
{
    struct mount_args {
        const char *type;
        const char *dir;
        int flags;
        void *data;
    } __attribute__((packed)) args = {
        type, dir, flags, data
    };

    int ret;
    SYSCALL1(ret, SYS_MOUNT, &args);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

int mkdir(const char *path, mode_t mode)
{
    int ret;
    SYSCALL2(ret, SYS_MKDIR, path, mode);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

int uname(struct utsname *name)
{
    int ret;
    SYSCALL1(ret, SYS_UNAME, name);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

int pipe(int fd[2])
{
    int ret;
    SYSCALL1(ret, SYS_PIPE, fd);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

int fcntl(int fildes, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    void *arg = va_arg(ap, void *); /* FIXME */
    va_end(ap);

    int ret;
    SYSCALL3(ret, SYS_PIPE, fildes, cmd, arg);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

int chdir(const char *path)
{
    int ret;
    SYSCALL1(ret, SYS_CHDIR, path);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

int getcwd(char *buf, size_t size)
{
    int ret;
    SYSCALL2(ret, SYS_GETCWD, buf, size);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

int setpgid(pid_t pid, pid_t pgid)
{
    int ret;
    SYSCALL2(ret, SYS_SETPGID, pid, pgid);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

int mknod(const char *path, mode_t mode, dev_t dev)
{
    int ret;
    SYSCALL3(ret, SYS_MKNOD, path, mode, dev);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}

int lstat(const char *path, struct stat *buf)
{
    int ret;
    SYSCALL2(ret, SYS_LSTAT, path, buf);

    if (ret < 0) {
        errno = -ret;
        return -1;
    }

    return 0;
}
