#ifndef _DEV_I8042_H
#define _DEV_I8042_H

#include <core/types.h>

/* dev/input/i8042.c: the PC's keyboard controller */
void i8042_handler_register(int port, void (*fun)(int));  /* port 1, 2 */
int  i8042_aux_write(uint8_t byte);
void i8042_reboot(void);

#endif /* ! _DEV_I8042_H */
