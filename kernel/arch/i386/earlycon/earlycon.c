/** \file
 * x86: the early console, where `printk()` writes before the kernel's
 * terminals exist.
 *
 * It starts on the first serial port, and once the kernel's command line
 * is read (`earlycon_reinit()`), moves to the boot loader's framebuffer
 * if there is one. `earlycon=ttyS0`, `earlycon=vga` or `earlycon=fb`
 * chooses it explicitly.
 */

#include <core/kargs.h>
#include <core/string.h>
#include <console/earlycon.h>

static struct earlycon *earlycon;

extern struct earlycon earlycon_uart;
extern struct earlycon earlycon_vga;
extern struct earlycon earlycon_fb;

extern int earlycon_fb_available(void);

/*
 * Writes a string on the early console.
 */
int earlycon_puts(char *s)
{
    return earlycon->puts(s);
}

/*
 * Writes a character on the early console.
 */
int earlycon_putc(char c)
{
    return earlycon->putc(c);
}

/*
 * Says whether the early console writes on the screen rather than on the
 * serial port.
 */
int earlycon_on_screen(void)
{
    return earlycon != &earlycon_uart;
}

/* Starts the early console, on the first serial port, at boot. */
void earlycon_init()
{
    earlycon = &earlycon_uart;
    earlycon->init();
}

/* Chooses the early console again once the kernel's command line is
 * read: the framebuffer if there is one, unless `earlycon=` says
 * otherwise. */
void earlycon_reinit()
{
    const char *arg_earlycon = NULL;
    /* the framebuffer if there is one, else the serial port */
    earlycon = earlycon_fb_available() ? &earlycon_fb : &earlycon_uart;

    if (!kargs_get("earlycon", &arg_earlycon) && arg_earlycon) {
        if (!strcmp(arg_earlycon, "ttyS0"))
            earlycon = &earlycon_uart;
        else if (!strcmp(arg_earlycon, "vga"))
            earlycon = &earlycon_vga;
        else if (!strcmp(arg_earlycon, "fb") && earlycon_fb_available())
            earlycon = &earlycon_fb;
    }

    earlycon->init();
}
