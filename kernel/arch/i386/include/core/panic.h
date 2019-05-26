#ifndef _PANIC_H
#define _PANIC_H

#include <core/system.h>
#include <core/arch.h>
#include <core/printk.h>

#define __PANIC_MSG "Bailing out. You are on your own. Good luck."

#define panic(s) \
{\
    printk("KERNEL PANIC: %s\n%s[%d]: %s\n", \
        s, __FILE__, __LINE__, __func__);\
    printk("%s\n", __PANIC_MSG); \
    asm("cli"); \
    for(;;); \
}\

#endif /* ! _PANIC_H */
