#ifndef _DEV_I8042_H
#define _DEV_I8042_H

#include <core/types.h>

/* The PC's keyboard controller (dev/input/i8042.c). */

/* Registers the function that gets each byte from port 1 (the keyboard)
 * or port 2 (the mouse). */
void i8042_handler_register(int port, void (*fun)(int));

/* Sends a byte to the mouse. */
int  i8042_aux_write(uint8_t byte);

/* Resets the CPU, through the controller. */
void i8042_reboot(void);

#endif /* ! _DEV_I8042_H */
