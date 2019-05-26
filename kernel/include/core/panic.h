#ifndef _CORE_PANIC_H
#define _CORE_PANIC_H

#include <core/system.h>
#include <core/printk.h>
#include <core/arch.h>

#define __PANIC_MSG "Bailing out. You are on your own. Good luck.\n"

#define panic(s) \
{\
    printk("KERNEL PANIC:\n%s [%d] %s: %s\n" __PANIC_MSG, \
        __FILE__, __LINE__, __func__, s);\
    arch_stacktrace(); \
    arch_disable_interrupts(); \
    for(;;); \
}\

#endif /* ! _CORE_PANIC_H */
