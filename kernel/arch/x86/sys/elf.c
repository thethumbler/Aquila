#include <core/system.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <sys/proc.h>
#include <core/printk.h>

#include "sys.h"

void *arch_load_elf()
{
    static struct arch_load_elf ret;

    ret.cur = get_current_page_directory();
    ret.new = get_new_page_directory();

    pmman.switch_mapping(ret.new);

    return &ret;
}

void arch_load_elf_end(void *d)
{
    struct arch_load_elf *p = (struct arch_load_elf *) d;
    pmman.switch_mapping(p->cur);
}
