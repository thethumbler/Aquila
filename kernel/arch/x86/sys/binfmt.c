#include <core/system.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <sys/proc.h>
#include <core/printk.h>

#include "sys.h"

void *arch_binfmt_load()
{
    static struct arch_binfmt ret;

    ret.cur = get_current_page_directory();
    ret.new = get_new_page_directory();

    arch_switch_mapping(ret.new);

    return &ret;
}

void arch_binfmt_end(void *d)
{
    struct arch_binfmt *p = (struct arch_binfmt *) d;
    arch_switch_mapping(p->cur);
}
