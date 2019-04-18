#ifndef _PANIC_H
#define _PANIC_H

#include <core/system.h>
#include <core/printk.h>

#define __PANIC_MSG "Bailing out. You are on your own. Good luck."

#if 0
struct stack_frame {
    struct stack_frame *ebp;
    uintptr_t eip;
};

static void stack_trace()
{
    printk("--- BEGIN STACK TRACE ---\n");

    struct stack_frame *stk;
    asm volatile ("movl %%ebp, %0":"=r"(stk));
    printk("stack frame %p\n", stk);

    for (int i = 0; stk && i < 1; ++i) {
        printk("eip = %p\n", stk->eip);
        stk = stk->ebp;
    }

    printk("--- END STACK TRACE ---\n");
}
#endif

#define panic(s) \
{\
    printk("KERNEL PANIC: %s\n%s[%d]: %s\n", \
        s, __FILE__, __LINE__, __func__);\
    printk("%s\n", __PANIC_MSG); \
    asm("cli"); \
    for(;;); \
}\

#endif /* ! _PANIC_H */
