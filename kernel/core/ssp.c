/** \file
 * What the compiler's stack protector (`-fstack-protector`) needs: the
 * canary value it puts on the stack, and the function it calls when a
 * function returns with that value overwritten.
 */

#include <core/system.h>
#include <core/panic.h>

uintptr_t __stack_chk_guard = 0xDEADBEEF;

/**
 * Panics: a function's stack canary was overwritten, so its stack was
 * smashed (a buffer overflow).
 */
void __stack_chk_fail(void)
{
    panic("stack smashing detected");
}
