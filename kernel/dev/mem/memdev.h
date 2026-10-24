#ifndef _MEMDEV_H
#define _MEMDEV_H

/*
 * The memory devices, the character devices of major 1 (see memdev.c):
 * each one's driver, which memdev_mux() picks by minor.
 */

#include <dev/dev.h>

extern struct dev nulldev;
extern struct dev zerodev;
extern struct dev randomdev;
extern struct dev kmsgdev;

#endif /* ! _MEMDEV_H */
