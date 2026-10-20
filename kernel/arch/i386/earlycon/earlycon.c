#include <core/kargs.h>
#include <core/string.h>
#include <console/earlycon.h>

static struct earlycon *earlycon;

extern struct earlycon earlycon_uart;
extern struct earlycon earlycon_vga;
extern struct earlycon earlycon_fb;

extern int earlycon_fb_available(void);

int earlycon_puts(char *s)
{
    return earlycon->puts(s);
}

int earlycon_putc(char c)
{
    return earlycon->putc(c);
}

/* whether it shows messages on the screen, not the serial port */
int earlycon_on_screen(void)
{
    return earlycon != &earlycon_uart;
}

void earlycon_init()
{
    earlycon = &earlycon_uart;
    earlycon->init();
}

void earlycon_reinit()
{
    const char *arg_earlycon = NULL;
    /* default to framebuffer if we have one, otherwise stay on uart */
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
