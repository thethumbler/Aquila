/** \file
 * `printk()`: the kernel's messages.
 *
 * A message goes to three places: the kernel's message buffer (`kmsg`,
 * which `/dev/kmsg` reads, the oldest messages overwritten when it's
 * full), the console if one has registered (`printk_console()`), and the
 * early console until it's disabled. `printk()` understands a small set
 * of formats: `%c`, `%s`, `%d` and `%u` (32-bit), `%ld` and `%lu`
 * (64-bit), `%x` and `%lx` (hexadecimal), `%b` (binary) and `%p`. The
 * `l` formats take a 64-bit argument on every architecture: cast a
 * `long` to `int64_t` or `uint64_t`.
 */

#include <core/system.h>
#include <ds/queue.h>
#include <sys/waitq.h>

#include <core/string.h>
#include <console/earlycon.h>
#include <ds/ringbuf.h>

/* The kernel's message buffer, and the readers of /dev/kmsg waiting for
 * messages. */
#define KMSG_SIZE   8192
struct ringbuf *kmsg = RINGBUF_NEW(KMSG_SIZE);
struct waitq *kmsg_wait = &(struct waitq){0};

static int use_earlycon = 1;

/**
 * Stops sending the kernel's messages to the early console, once a real
 * console has taken over.
 */
void earlycon_disable()
{
    use_earlycon = 0;
}

static void (*console_write)(const char *s, size_t len);

/**
 * Registers the console that the kernel's messages are written to, besides
 * the message buffer.
 *
 * @param write  the console's write function
 */
void printk_console(void (*write)(const char *s, size_t len))
{
    console_write = write;
}

/** Writes one character of a message. Returns what the early console's
 * write returned, or 0. */
static int putc(char c)
{
    ringbuf_write_overwrite(kmsg, 1, &c);

    if (console_write)
        console_write(&c, 1);

    if (use_earlycon)
        return earlycon_putc(c);
    return 0;
}

/** Writes a string of a message ("(null)" for NULL). */
static int puts(char *s)
{
    s = s? s : "(null)";

    int len = strlen(s);
    ringbuf_write_overwrite(kmsg, len, s);

    if (console_write)
        console_write(s, len);

    if (use_earlycon)
        return earlycon_puts(s);

    return 0;
}

/** Writes a 32-bit number in hexadecimal, without leading zeros. */
static int putx(uint32_t val)
{
    if (!val)
        return puts("0");

    char *enc = "0123456789ABCDEF";
    char buf[9];
    buf[8] = '\0';
    uint8_t i = 8;

    while (val && i) {
        buf[--i] = enc[val & 0xF];
        val >>= 4;
    }

    return puts(&buf[i]);
}

/** Writes a 64-bit number in hexadecimal, all 16 digits. */
static int putlx(uint64_t val)
{
    char *enc = "0123456789ABCDEF";
    char buf[17];
    buf[16] = '\0';
    uint8_t i = 16;
    while (i) {
        buf[--i] = enc[val&0xF];
        val >>= 4;
    }
    return puts(buf);
}

/** Writes an unsigned 32-bit number in decimal. */
static int putud(uint32_t val)
{
    char buf[11];
    buf[10] = '\0';
    if (!val) { buf[9] = '0'; return puts(&buf[9]); }
    uint8_t i = 10;
    while (val) {
        buf[--i] = val%10 + '0';
        val = (val-val%10)/10;
    }
    return puts(buf+i);
}

/** Writes an unsigned 64-bit number in decimal. */
static int putul(uint64_t val)
{
    char buf[21];
    buf[20] = '\0';
    if (!val) { buf[19] = '0'; return puts(&buf[19]); }
    uint8_t i = 20;
    while (val) {
        buf[--i] = val%10 + '0';
        val = (val-val%10)/10;
    }
    return puts(buf+i);
}

/** Writes a byte in binary, all 8 digits. */
static int putb(uint8_t val)
{
    char buf[9];
    buf[8] = '\0';
    uint8_t i = 8;
    while (i) {
        buf[--i] = '0' + (val & 1);
        val >>= 1;
    }
    return puts(buf);
}

/**
 * Writes a kernel message, with its arguments in a `va_list`.
 *
 * @param fmt   the format (see the top of this file for what it supports)
 * @param args  the arguments
 * @return what the early console's writes returned, added up
 */
int vprintk(const char *fmt, va_list args)
{
    int ret = 0;
    while (*fmt)
    switch (*fmt) {
        case '%':
            ++fmt;
            switch (*fmt) {
                case 'c':   /* a character */
                    ret += putc((char)va_arg(args, int));
                    break;
                case 's':   /* a string */
                    ret += puts((char*)va_arg(args, char*));
                    break;
                case 'd':   /* a signed decimal */
                    {
                        int32_t val = va_arg(args, int32_t);
                        if (val < 0) {
                            ret += putc('-');
                            ret += putud(-(uint32_t) val);
                        } else {
                            ret += putud((uint32_t) val);
                        }
                    }
                    break;
                case 'u':   /* an unsigned decimal */
                    ret += putud((uint32_t)va_arg(args, uint32_t));
                    break;
                case 'l':   /* a 64-bit number */
                    switch (*++fmt) {
                        case 'x':   /* in hexadecimal */
                            ret += putlx((uint64_t)va_arg(args, uint64_t));
                            break;
                        case 'd':   /* a signed decimal */
                            {
                                int64_t val = va_arg(args, int64_t);
                                if (val < 0) {
                                    ret += putc('-');
                                    ret += putul(-(uint64_t) val);
                                } else {
                                    ret += putul((uint64_t) val);
                                }
                            }
                            break;
                        case 'u':   /* an unsigned decimal */
                            ret += putul((uint64_t)va_arg(args, uint64_t));
                            break;
                        default:
                            ret += putc(*--fmt);
                    }
                    break;
                
                case 'b':   /* a byte in binary */
                    ret += putb((uint8_t)(uint32_t)va_arg(args, uint32_t));
                    break;
                case 'x':   /* hexadecimal */
                    ret += putx((uint32_t)va_arg(args, uint32_t));
                    break;
                case 'p':   /* a pointer */
                    ret += puts("0x");
#if ARCH_BITS==32
                    ret += putx((uint32_t)va_arg(args, uint32_t));
#elif ARCH_BITS==64 
                    ret += putlx((uint64_t)va_arg(args, uint64_t));
#endif
                    break;
                default:
                    ret += putc(*(--fmt));
            }
            ++fmt;
            break;
        default:
            ret += putc(*fmt);
            ++fmt;
    }

    return ret;
}

/**
 * Writes a kernel message, to the message buffer and the consoles.
 *
 * @param fmt  the format (see the top of this file for what it supports)
 * @return what the early console's writes returned, added up
 */
int printk(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vprintk(fmt, args);
    va_end(args);

    return ret;
}
