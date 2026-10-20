#ifndef _AQUILA_REBOOT_H
#define _AQUILA_REBOOT_H

/* reboot(2) commands, the values of Linux */
#define RB_AUTOBOOT     0x01234567  /* restart */
#define RB_HALT_SYSTEM  0xcdef0123  /* halt */
#define RB_POWER_OFF    0x4321fedc  /* power off */
#define RB_ENABLE_CAD   0x89abcdef  /* Ctrl+Alt+Del restarts */
#define RB_DISABLE_CAD  0           /* Ctrl+Alt+Del sends SIGINT to init */

#endif /* ! _AQUILA_REBOOT_H */
