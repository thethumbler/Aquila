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

#include <mm/pmap.h>
#include <mm/mm.h>

void arch_mm_setup(void)
{
    pmap_init();
}
