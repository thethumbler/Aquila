/*
 * Pseudo-terminals: /dev/ptmx opens a master, its slave is
 * /dev/pts/<TIOCGPTN>, unlocked by TIOCSPTLCK. The slave's owner and mode
 * are already right (devpts), grantpt() only checks the descriptor.
 * openpty() makes both at once.
 */

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
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

/* a master and its slave, set up with termp and winp if given; the
 * slave's name in name (glibc doesn't say how long it may be either) */
int openpty(int *amaster, int *aslave, char *name,
            const struct termios *termp, const struct winsize *winp)
{
    char path[32];
    int master, slave;

    if ((master = posix_openpt(O_RDWR | O_NOCTTY)) < 0)
        return -1;

    if (grantpt(master) < 0 || unlockpt(master) < 0 ||
        ptsname_r(master, path, sizeof(path)) ||
        (slave = open(path, O_RDWR | O_NOCTTY)) < 0) {
        int err = errno;
        close(master);
        errno = err;
        return -1;
    }

    if (termp)
        tcsetattr(slave, TCSANOW, termp);

    if (winp)
        ioctl(slave, TIOCSWINSZ, winp);

    if (name)
        strcpy(name, path);

    *amaster = master;
    *aslave = slave;
    return 0;
}
