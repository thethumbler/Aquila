#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

/* request numbers and struct winsize, shared with the kernel */
#include <aquila/ioctl.h>

int ioctl(int fildes, int request, ...);

#endif /* ! _SYS_IOCTL_H */
