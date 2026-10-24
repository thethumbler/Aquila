/** \file
 * x86: what the architecture adds to setting up memory at boot.
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

/*
 * Finishes setting up memory at boot, once `mm_setup()` has set up the
 * page structures and the buddy allocator: it keeps the trampoline's
 * pages out of the allocator, and takes over the boot page tables.
 */
void arch_mm_setup(void)
{
    /* Never give out the trampoline's pages, where the other CPUs start:
     * they are in conventional memory, which is free on PCs. */
    buddy_set_unusable(X86_TRAMPOLINE, X86_TRAMPOLINE_SIZE);

    pmap_init();
}
