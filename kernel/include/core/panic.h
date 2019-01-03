#ifndef _PANIC_H
#define _PANIC_H

#include <core/system.h>
#include <core/printk.h>
#include <core/arch.h>

#define panic(s) \
{\
    printk("KERNEL PANIC:\n%s [%d] %s: %s\n" __PANIC_MSG, \
        __FILE__, __LINE__, __func__, s);\
    arch_disable_interrupts(); \
    for(;;); \
}\

#endif /* !_PANIC_H */
