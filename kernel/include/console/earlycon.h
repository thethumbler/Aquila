#ifndef _EARLY_CONSOLE_H
#define _EARLY_CONSOLE_H

/*
 * The early console: where the kernel's messages go from the first
 * moment of boot, before any driver is up (the serial port, or the
 * screen). Each architecture has its own (arch/ARCH/earlycon/).
 */

/** An early console's operations. */
struct earlycon {
    void (*init)(void);
    int  (*puts)(char *);
    int  (*putc)(char);
};

/** Sets up the early console at boot, before anything else. */
void earlycon_init(void);

/** Sets up the early console again, once the kernel's command line has
 * been read, since it may choose another console (`earlycon=`). */
void earlycon_reinit(void);

/**
 * Writes a string on the early console.
 *
 * @param s  the string
 * @return how many characters were written
 */
int  earlycon_puts(char *s);

/**
 * Writes a character on the early console.
 *
 * @param c  the character
 * @return 1 if it was written, 0 if not
 */
int  earlycon_putc(char c);

/**
 * Says whether the early console writes on the screen, rather than on a
 * serial port.
 *
 * @return 1 if it writes on the screen, 0 if not
 */
int  earlycon_on_screen(void);

/* Stops writing the kernel's messages to the early console (core/printk.c). */
void earlycon_disable(void);

/* Registers a console that shows the kernel's messages from now on. */
void printk_console(void (*write)(const char *s, size_t len));

#endif
