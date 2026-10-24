#ifndef _DEV_TTYDEV_H
#define _DEV_TTYDEV_H

#include <core/system.h>
#include <dev/dev.h>

/* The terminal drivers: /dev/tty, the virtual terminals, the
 * pseudo-terminals' masters, and the serial ports. */
extern struct dev sttydev;
extern struct dev ttydev;
extern struct dev ptmdev;
extern struct dev uart;

#endif /* ! _DEV_TTYDEV_H */
