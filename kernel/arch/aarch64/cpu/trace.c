/** \file
 * A panic's backtrace: the frame records (x29 points to the caller's x29
 * and its return address, AAPCS64), while they're in the kernel's half
 */

#include <core/system.h>
#include <mm/mm.h>

int __printing_trace = 0;

void arch_stack_trace(void)
{
    __printing_trace = 1;
    printk("stack trace:\n");

    uintptr_t *fp = __builtin_frame_address(0);

    for (int i = 0; i < 32 && fp && (uintptr_t) fp >= ARCH_DMAP_BASE; ++i) {
        printk("  [%p]\n", (void *) fp[1]);
        fp = (uintptr_t *) fp[0];
    }

    __printing_trace = 0;
}
