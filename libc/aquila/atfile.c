/*
 * The *at() calls, relative to a directory descriptor, and the calls
 * built on them (symlink, readlink, rename, utime, ...)
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <utime.h>

#include <aquila/syscall.h>

#include "syscall.h"

int openat(int dirfd, const char *path, int oflags, ...)
{
    mode_t mode = 0;

    if (oflags & O_CREAT) {
        va_list ap;
        va_start(ap, oflags);
        mode = va_arg(ap, int);
        va_end(ap);
    }

    return SYSCALL5(SYS_openat, dirfd, path, oflags, mode, 0);
}

int fstatat(int dirfd, const char *path, struct stat *buf, int flags)
{
    return SYSCALL5(SYS_fstatat, dirfd, path, buf, flags, 0);
}

int unlinkat(int dirfd, const char *path, int flags)
{
    return SYSCALL(SYS_unlinkat, dirfd, path, flags);
}

int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
    return SYSCALL5(SYS_mknodat, dirfd, path, mode, dev, 0);
}

int mkdirat(int dirfd, const char *path, mode_t mode)
{
    return mknodat(dirfd, path, S_IFDIR | (mode & 07777), 0);
}

int mkfifoat(int dirfd, const char *path, mode_t mode)
{
    return mknodat(dirfd, path, S_IFIFO | (mode & 07777), 0);
}

int mkfifo(const char *path, mode_t mode)
{
    return mkfifoat(AT_FDCWD, path, mode);
}

int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    return SYSCALL5(SYS_linkat, olddirfd, oldpath, newdirfd, newpath, flags);
}

int symlinkat(const char *target, int dirfd, const char *path)
{
    return SYSCALL(SYS_symlinkat, target, dirfd, path);
}

int symlink(const char *target, const char *path)
{
    return symlinkat(target, AT_FDCWD, path);
}

ssize_t readlinkat(int dirfd, const char *restrict path, char *restrict buf, size_t size)
{
    return SYSCALL5(SYS_readlinkat, dirfd, path, buf, size, 0);
}

ssize_t readlink(const char *restrict path, char *restrict buf, size_t size)
{
    return readlinkat(AT_FDCWD, path, buf, size);
}

int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
    return SYSCALL5(SYS_renameat, olddirfd, oldpath, newdirfd, newpath, 0);
}

/* newlib's rename() (built with HAVE_RENAME) */
int _rename(const char *oldpath, const char *newpath)
{
    return renameat(AT_FDCWD, oldpath, AT_FDCWD, newpath);
}

int faccessat(int dirfd, const char *path, int mode, int flags)
{
    return SYSCALL5(SYS_faccessat, dirfd, path, mode, flags, 0);
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    return SYSCALL5(SYS_fchmodat, dirfd, path, mode, flags, 0);
}

int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags)
{
    return SYSCALL5(SYS_fchownat, dirfd, path, owner, group, flags);
}

int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
    return SYSCALL5(SYS_utimensat, dirfd, path, times, flags, 0);
}

int futimens(int fildes, const struct timespec times[2])
{
    return SYSCALL5(SYS_utimensat, fildes, NULL, times, 0, 0);
}

int utimes(const char *path, const struct timeval times[2])
{
    struct timespec ts[2];

    if (!times)
        return utimensat(AT_FDCWD, path, NULL, 0);

    for (int i = 0; i < 2; ++i) {
        ts[i].tv_sec  = times[i].tv_sec;
        ts[i].tv_nsec = times[i].tv_usec * 1000;
    }

    return utimensat(AT_FDCWD, path, ts, 0);
}

int utime(const char *path, const struct utimbuf *times)
{
    struct timespec ts[2];

    if (!times)
        return utimensat(AT_FDCWD, path, NULL, 0);

    ts[0] = (struct timespec) {times->actime, 0};
    ts[1] = (struct timespec) {times->modtime, 0};

    return utimensat(AT_FDCWD, path, ts, 0);
}
