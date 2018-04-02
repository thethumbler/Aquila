#ifndef _EARLY_CONSOLE_H
#define _EARLY_CONSOLE_H

struct __earlycon {
    void (*init)();
    int  (*puts)(char*);
    int  (*putc)(char);
};

void earlycon_init();
int  earlycon_puts(char*);
int  earlycon_putc(char);

#endif
