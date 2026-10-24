/** \file
 * The PS/2 keyboard driver.
 *
 * The keyboard is on the first port of the i8042 controller
 * (`dev/input/i8042.c`). Each byte it sends, a scancode, goes to the
 * virtual terminals (`vt_scancode()`), which turn it into keys and
 * characters.
 */

#include <core/system.h>
#include <core/module.h>
#include <cpu/cpu.h>

#include <dev/i8042.h>

#include <dev/vt.h>

/** Sends the keyboard's scancodes to the virtual terminals. */
static int ps2kbd_probe(void)
{
    i8042_handler_register(1, vt_scancode);
    return 0;
}

MODULE_INIT(ps2kbd, ps2kbd_probe, NULL)
