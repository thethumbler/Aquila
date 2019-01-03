#include <core/system.h>
#include <cpu/cpu.h>
#include <mm/mm.h>
#include <sys/proc.h>

static inline void switch_page_directory(uintptr_t pd)
{
    arch_switch_mapping(pd);
}

void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));

#define PUSH(stack, type, value)\
    do {\
        (stack) -= sizeof(type);\
        *((type *) (stack)) = (type) (value);\
    } while (0)
