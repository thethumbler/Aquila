#include <core/system.h>
#include <cpu/cpu.h>
#include <mm/mm.h>
#include <sys/proc.h>

/* Common types for sys */
struct arch_binfmt {
    uintptr_t cur;
    uintptr_t new;
};

/* Common functions for sys */
static inline uintptr_t get_current_page_directory()
{
    return read_cr3() & ~PAGE_MASK;
}

static inline uintptr_t get_new_page_directory()
{
    /* Get a free page frame for storing Page Directory */
    return arch_get_frame();
}

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
