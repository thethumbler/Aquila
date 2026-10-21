#ifndef _PTY_H
#define _PTY_H

/* a pseudo-terminal in one call (BSD, as glibc's) */

#include <termios.h>
#include <sys/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

int openpty(int *amaster, int *aslave, char *name,
            const struct termios *termp, const struct winsize *winp);

#ifdef __cplusplus
}
#endif

#endif /* ! _PTY_H */
