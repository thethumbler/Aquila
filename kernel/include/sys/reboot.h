#ifndef _SYS_REBOOT_H
#define _SYS_REBOOT_H

#include <aquila/reboot.h>

/* Restarts, halts or powers off the machine (sys/reboot.c), and handles
 * Ctrl+Alt+Del. */
int  kernel_reboot(unsigned cmd);
void kernel_ctrlaltdel(void);

#endif /* ! _SYS_REBOOT_H */
