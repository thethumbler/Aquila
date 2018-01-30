#ifndef _IOCTL_H
#define _IOCTL_H

int ioctl(int fildes, int request, ...);

#define TIOCGPTN    0x80045430 

#endif
