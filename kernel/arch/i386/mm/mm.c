/**********************************************************************
 *                  Physical Memory Manager
 */

#include <core/system.h>
#include <core/string.h>
#include <core/panic.h>
#include <core/arch.h>

#include <cpu/cpu.h>

#include <boot/multiboot.h>
#include <boot/boot.h>

#include <cpu/smp.h>
#include <mm/buddy.h>
#include <mm/pmap.h>
#include <mm/mm.h>

void arch_mm_setup(void)
{
    /* the trampoline's pages (where the other CPUs start), never given
     * out: conventional memory, free on PCs */
    buddy_set_unusable(X86_TRAMPOLINE, X86_TRAMPOLINE_SIZE);

    pmap_init();
}
