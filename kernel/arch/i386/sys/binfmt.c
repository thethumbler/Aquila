#include <core/system.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <sys/proc.h>
#include <core/printk.h>

void *arch_binfmt_load()
{
    static struct arch_binfmt ret;

    ret.cur_map = read_cr3() & ~PAGE_MASK;
    ret.new_map = arch_get_frame();

    arch_switch_mapping(ret.new_map);

    return &ret;
}

void arch_binfmt_end(void *d)
{
    struct arch_binfmt *p = (struct arch_binfmt *) d;
    arch_switch_mapping(p->cur_map);
}
