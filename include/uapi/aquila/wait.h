#ifndef _AQUILA_WAIT_H
#define _AQUILA_WAIT_H

/* waitpid(2) options, the C library defines these too */
#ifndef WNOHANG
#define WNOHANG     1   /* don't wait */
#define WUNTRACED   2   /* report stopped children */
#define WCONTINUED  8   /* report continued children */
#endif

/*
 * Status words: exited (code << 8), killed by a signal (signal), stopped
 * by a signal ((signal << 8) | 0x7f), continued (0xffff).
 */

#endif /* ! _AQUILA_WAIT_H */
