#ifndef _SYS_REBOOT_H
#define _SYS_REBOOT_H

/* RB_* commands, shared with the kernel */
#include <aquila/reboot.h>

#ifdef __cplusplus
extern "C" {
#endif

int reboot(int cmd);

#ifdef __cplusplus
}
#endif

#endif /* ! _SYS_REBOOT_H */
