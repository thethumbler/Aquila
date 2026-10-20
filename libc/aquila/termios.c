#include <termios.h>
#include <sys/ioctl.h>

int tcgetattr(int fd, struct termios *tios)
{
    return ioctl(fd, TCGETS, tios);
}

int tcsetattr(int fd, int optional_actions, const struct termios *tios)
{
    /* TCSADRAIN and TCSAFLUSH are treated as TCSANOW */
    return ioctl(fd, TCSETS, tios);
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
