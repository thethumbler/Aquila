/** \file
 * A panic's backtrace: the frame pointers, each frame's saved %ebp/%rbp
 * and return address. Each address is named from the kernel's symbol
 * table when the boot loader passed it (Limine does, QEMU's -kernel
 * doesn't); mk/backtrace.sh names them from the kernel's file otherwise.
 */

#include <core/system.h>
#include <core/printk.h>
#include <boot/boot.h>

struct stack_frame {
    struct stack_frame *bp;
    uintptr_t ip;
};

extern struct boot *__kboot;

int __printing_trace = 0;

/** the function `ip' is in, NULL if there are no symbols or none has it */
static const elf_sym_t *symbol_of(uintptr_t ip)
{
    if (!__kboot || !__kboot->symtab || !__kboot->strtab)
        return NULL;

    const elf_sym_t *sym = (const elf_sym_t *) (uintptr_t) __kboot->symtab->sh_addr;

    for (size_t i = 0; i < __kboot->symnum; ++i, ++sym)
        if (ELF32_ST_TYPE(sym->st_info) == STT_FUNC &&
            ip >= sym->st_value && ip < sym->st_value + sym->st_size)
            return sym;

    return NULL;
}

void arch_stack_trace(void)
{
    __printing_trace = 1;

    struct stack_frame *stk = NULL;

#if ARCH_BITS==32
    asm volatile ("movl %%ebp, %0":"=r"(stk));
#else
    asm volatile ("movq %%rbp, %0":"=r"(stk));
#endif

    printk("stack trace:\n");

    for (int frame = 0; stk && frame < 20; ++frame) {
        const elf_sym_t *sym = symbol_of(stk->ip);

        if (sym) {
            const char *names = (const char *) (uintptr_t) __kboot->strtab->sh_addr;
            printk("  [%p] %s+0x%lx\n", stk->ip, names + sym->st_name, (unsigned long) (stk->ip - sym->st_value));
        } else {
            printk("  [%p]\n", stk->ip);
        }

        stk = stk->bp;
    }
}
