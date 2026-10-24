/** \file
 * aarch64: a panic's stack trace, from the frame records (AAPCS64): x29
 * points to the caller's x29 and its return address. The walk stops when
 * a record isn't in the kernel's half. mk/backtrace.sh names the
 * addresses.
 */

#include <core/system.h>
#include <mm/mm.h>

int __printing_trace = 0;

/* Prints the current stack trace, the return address of each frame. */
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
