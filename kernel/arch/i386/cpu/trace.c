#include <core/system.h>
#include <core/printk.h>
#include <boot/boot.h>

struct stack_frame {
    struct stack_frame *bp;
    uintptr_t ip;
};

extern struct boot *__kboot;

int __printing_trace = 0;

void arch_stack_trace()
{
#if 0
    __printing_trace = 1;

    struct stack_frame *stk;

    asm volatile ("movl %%ebp, %0":"=r"(stk));

    printk("stack trace:\n");

    const char *strs = (const char *) __kboot->strtab->sh_addr;

    for (int frame = 0; stk && frame < 20; ++frame) {

        /* find symbol */
        struct elf32_sym *sym = (struct elf32_sym *) __kboot->symtab->sh_addr;

        int found = 0;

        for (size_t i = 0; i < __kboot->symnum; ++i) {
            if (ELF32_ST_TYPE(sym->st_info) == STT_FUNC) {
                if (stk->ip > sym->st_value && stk->ip < sym->st_value + sym->st_size) {
                    found = 1;
                    break;
                }
            }
            ++sym;
        }

        if (found) {
            off_t off = stk->ip - sym->st_value;
            printk("  [%p] %s+0x%x\n", stk->ip, strs + sym->st_name, off);
        } else {
            printk("  [%p]\n", stk->ip);
        }

        stk = stk->bp;
    }
#endif
}
