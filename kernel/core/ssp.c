/** \file
 * The stack protector's (-fstack-protector): the canary, and what a
 * smashed one ends in
 */

#include <core/system.h>
#include <core/panic.h>

uintptr_t __stack_chk_guard = 0xDEADBEEF;

void __stack_chk_fail(void)
{
    panic("stack smashing detected");
}
