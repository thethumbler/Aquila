#include <console/earlycon.h>

static struct __earlycon *__earlycon;

int earlycon_puts(char *s)
{
    return __earlycon->puts(s);
}

int earlycon_putc(char c)
{
    return __earlycon->putc(c);
}

void earlycon_init()
{
    extern struct __earlycon __earlycon_uart;
    __earlycon = &__earlycon_uart;
    __earlycon->init();
}
