#ifndef _CORE_PANIC_H
#define _CORE_PANIC_H

#include <core/system.h>
#include <core/printk.h>
#include <core/arch.h>

#define __PANIC_MSG "Bailing out. You are on your own. Good luck.\n"

/* Stops the kernel for good, when it finds itself in a state it can't
 * go on from: prints where and why, and a stack trace, then halts the
 * CPU with its interrupts disabled. */
#define panic(s) \
{\
    printk("KERNEL PANIC:\n%s [%d] %s: %s\n" __PANIC_MSG, \
        __FILE__, __LINE__, __func__, s);\
    arch_stack_trace(); \
    arch_interrupts_disable(); \
    for(;;); \
}\

#endif /* ! _CORE_PANIC_H */
