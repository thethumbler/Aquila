#include <core/kargs.h>
#include <core/string.h>
#include <console/earlycon.h>

static struct earlycon *earlycon;

extern struct earlycon earlycon_uart;
extern struct earlycon earlycon_vga;
extern struct earlycon earlycon_fb;

int earlycon_puts(char *s)
{
    return earlycon->puts(s);
}

int earlycon_putc(char c)
{
    return earlycon->putc(c);
}

void earlycon_init()
{
    earlycon = &earlycon_uart;
    earlycon->init();
}

void earlycon_reinit()
{
    const char *arg_earlycon;
    earlycon = &earlycon_fb; /* default */

    if (!kargs_get("earlycon", &arg_earlycon)) {
        if (!strcmp(arg_earlycon, "ttyS0"))
            earlycon = &earlycon_uart;
        else if (!strcmp(arg_earlycon, "vga"))
            earlycon = &earlycon_vga;
        else if (!strcmp(arg_earlycon, "fb"))
            earlycon = &earlycon_fb;
    }

    earlycon->init();
}
