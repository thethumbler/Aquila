#include <core/system.h>
#include <cpu/cpu.h>
#include <mm/mm.h>
#include <sys/proc.h>

/* x86: what arch/i386/sys/ shares. */

/* Start running in user mode, and jump to an address with a frame pointer
 * and a stack (cpu.S). */
void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));

/* clock.c */
void x86_clock_init(void);
void x86_clock_ticks(uint32_t period_ns);
void x86_clock_tick(void);
const char *x86_clock_source(void);

/* Pushes `value`, of `type`, on the stack at `stack`. */
#define PUSH(stack, type, value)\
    do {\
        (stack) -= sizeof(type);\
        *((type *) (stack)) = (type) (value);\
    } while (0)
