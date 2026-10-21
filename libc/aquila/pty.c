/*
 * Pseudo-terminals: /dev/ptmx opens a master, its slave is
 * /dev/pts/<TIOCGPTN>, unlocked by TIOCSPTLCK. The slave's owner and mode
 * are already right (devpts), grantpt() only checks the descriptor.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

int posix_openpt(int flags)
{
    return open("/dev/ptmx", flags);
}

int grantpt(int fd)
{
    int n;

    /* a master */
    if (ioctl(fd, TIOCGPTN, &n) < 0) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int unlockpt(int fd)
{
    int lock = 0;
    return ioctl(fd, TIOCSPTLCK, &lock);
}

int ptsname_r(int fd, char *buf, size_t size)
{
    int n;

    if (ioctl(fd, TIOCGPTN, &n) < 0)
        return errno = ENOTTY;

    if ((size_t) snprintf(buf, size, "/dev/pts/%d", n) >= size)
        return errno = ERANGE;

    return 0;
}

char *ptsname(int fd)
{
    static char name[32];
    return ptsname_r(fd, name, sizeof(name)) ? NULL : name;
}
