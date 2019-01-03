#ifndef _PANIC_H
#define _PANIC_H

#define panic(s) \
{\
    printk("KERNEL PANIC:\n%s [%d] %s: %s\n" __PANIC_MSG, \
        __FILE__, __LINE__, __func__, s);\
    asm("cli"); \
    for(;;); \
}\

#endif /* ! _PANIC_H */
