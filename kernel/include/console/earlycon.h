#ifndef _EARLY_CONSOLE_H
#define _EARLY_CONSOLE_H

/**
 * \brief early console
 */
struct earlycon {
    void (*init)(void);
    int  (*puts)(char *);
    int  (*putc)(char);
};

void earlycon_init(void);
void earlycon_reinit(void);
int  earlycon_puts(char*);
int  earlycon_putc(char);
int  earlycon_on_screen(void);

/* defined in core/printk.c */
void earlycon_disable(void);

/* a console that shows kernel messages from now on */
void printk_console(void (*write)(const char *s, size_t len));

#endif
