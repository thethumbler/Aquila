#ifndef _SYS_TERMIOS_H
#define _SYS_TERMIOS_H

#include <sys/types.h>

/* struct termios and its flags, shared with the kernel */
#include <aquila/termios.h>

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
speed_t cfgetispeed(const struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);
int cfsetispeed(struct termios *termios_p, speed_t speed);
int cfsetospeed(struct termios *termios_p, speed_t speed);
void cfmakeraw(struct termios *termios_p);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgrp);

#endif /* ! _SYS_TERMIOS_H */
