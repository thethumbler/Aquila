#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>

int tcgetattr(int fd, struct termios *tios)
{
    return ioctl(fd, TCGETS, tios);
}

int tcsetattr(int fd, int optional_actions, const struct termios *tios)
{
    switch (optional_actions) {
        case TCSANOW:   return ioctl(fd, TCSETS, tios);
        case TCSADRAIN: return ioctl(fd, TCSETSW, tios);
        case TCSAFLUSH: return ioctl(fd, TCSETSF, tios);
    }

    errno = EINVAL;
    return -1;
}

int tcflush(int fd, int queue)
{
    return ioctl(fd, TCFLSH, queue);
}

int tcdrain(int fd)
{
    return ioctl(fd, TCSBRK, 1);
}

int tcflow(int fd, int action)
{
    return ioctl(fd, TCXONC, action);
}

int tcsendbreak(int fd, int duration)
{
    /* the terminals have no line to break */
    return ioctl(fd, TCSBRK, 0);
}

pid_t tcgetsid(int fd)
{
    pid_t sid;

    if (ioctl(fd, TIOCGSID, &sid) == -1)
        return -1;

    return sid;
}

speed_t cfgetispeed(const struct termios *tios)
{
    return tios->__c_ispeed;
}

speed_t cfgetospeed(const struct termios *tios)
{
    return tios->__c_ospeed;
}

int cfsetispeed(struct termios *tios, speed_t speed)
{
    tios->__c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *tios, speed_t speed)
{
    tios->__c_ospeed = speed;
    return 0;
}

void cfmakeraw(struct termios *tios)
{
    tios->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                | INLCR | IGNCR | ICRNL | IXON);
    tios->c_oflag &= ~OPOST;
    tios->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tios->c_cflag &= ~(CSIZE | PARENB);
    tios->c_cflag |= CS8;
}

pid_t tcgetpgrp(int fd)
{
    pid_t pgrp;

    if (ioctl(fd, TIOCGPGRP, &pgrp) == -1)
        return -1;

    return pgrp;
}

int tcsetpgrp(int fd, pid_t pgrp)
{
    return ioctl(fd, TIOCSPGRP, &pgrp);
}
