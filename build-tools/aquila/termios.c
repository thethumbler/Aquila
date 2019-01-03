#include <termios.h>
#include <sys/ioctl.h>

int tcgetattr(int fd, struct termios *tios)
{
    return ioctl(fd, TCGETS, tios);
}

int tcsetattr(int fd, int req, const struct termios *tios)
{
    return ioctl(fd, TCSETS, tios);
}

speed_t cfgetispeed(const struct termios *termios_p)
{
    return termios_p->__c_ispeed;
}

speed_t cfgetospeed(const struct termios *termios_p)
{
    return termios_p->__c_ospeed;
}

int cfsetispeed(struct termios *termios_p, speed_t speed)
{
    termios_p->__c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *termios_p, speed_t speed)
{
    termios_p->__c_ospeed = speed;
    return 0;
}

void cfmakeraw(struct termios *termios_p)
{
    termios_p->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                | INLCR | IGNCR | ICRNL | IXON);
    termios_p->c_oflag &= ~OPOST;
    termios_p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    termios_p->c_cflag &= ~(CSIZE | PARENB);
    termios_p->c_cflag |= CS8;
}
