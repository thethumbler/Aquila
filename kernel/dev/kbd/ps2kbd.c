/** \file
 *          PS/2 Keyboard driver
 *
 * Key presses go to the virtual terminals.
 */

#include <core/system.h>
#include <core/module.h>
#include <cpu/cpu.h>

#include <platform/misc.h>

#include <dev/vt.h>

static int ps2kbd_probe(void)
{
    x86_i8042_handler_register(1, vt_scancode);
    return 0;
}

MODULE_INIT(ps2kbd, ps2kbd_probe, NULL)
