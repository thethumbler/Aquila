/** \file
 * `snprintf()` and `vsnprintf()`: formatting into a buffer, with the
 * formats `printk()` has: `%c`, `%s`, `%d` and `%u` (32-bit), `%ld` and
 * `%lu` (64-bit), `%x` and `%lx` (hexadecimal), `%b` (binary) and `%p`.
 * The `l` formats take a 64-bit argument on every architecture. Each
 * helper below writes at most `n` characters to `s`, and returns how many
 * it wrote.
 */

#include <core/system.h>

/** Writes one character, if there is room. */
static int snputc(char *s, size_t n, char c)
{
    if (n) {
        *s = c;
        return 1;
    }

    return 0;
}

/** Writes a string, as much of it as fits. */
static int snputs(char *s, size_t n, char *str)
{
    /* a NULL string is a bug, but printing it shouldn't cause another */
    if (!str)
        str = "(null)";

    unsigned i;
    for (i = 0; i < n && str[i]; ++i)
        s[i] = str[i];
    return i;
}

/** Writes a 32-bit number in hexadecimal, all 8 digits. */
static int snputx(char *s, size_t n, uint32_t val)
{
    char *enc = "0123456789ABCDEF";
    char buf[9];
    buf[8] = '\0';
    uint8_t i = 8;

    while (i) {
        buf[--i] = enc[val&0xF];
        val >>= 4;
    }

    return snputs(s, n, buf);
}

/** Writes a 64-bit number in hexadecimal, all 16 digits. */
static int snputlx(char *s, size_t n, uint64_t val)
{
    char *enc = "0123456789ABCDEF";
    char buf[17];
    buf[16] = '\0';
    uint8_t i = 16;
    while (i) {
        buf[--i] = enc[val&0xF];
        val >>= 4;
    }
    return snputs(s, n, buf);
}

/** Writes an unsigned 32-bit number in decimal. */
static int snputud(char *s, size_t n, uint32_t val)
{
    char buf[11];
    buf[10] = '\0';

    if (!val)
        return snputc(s, n, '0');

    uint8_t i = 10;

    while (val) {
        buf[--i] = val%10 + '0';
        val = (val-val%10)/10;
    }

    return snputs(s, n, buf+i);
}

/** Writes an unsigned 64-bit number in decimal. */
static int snputul(char *s, size_t n, uint64_t val)
{
    char buf[21];
    buf[20] = '\0';
    uint8_t i = 20;

    if (!val)
        return snputc(s, n, '0');

    while (val) {
        buf[--i] = val%10 + '0';
        val = (val-val%10)/10;
    }

    return snputs(s, n, buf+i);
}

/** Writes a byte in binary, all 8 digits. */
static int snputb(char *s, size_t n, uint8_t val)
{
    char buf[9];
    buf[8] = '\0';
    uint8_t i = 8;

    while (i) {
        buf[--i] = '0' + (val & 1);
        val >>= 1;
    }

    return snputs(s, n, buf);
}

/**
 * Formats a string into a buffer, with its arguments in a `va_list`.
 *
 * @param s     the buffer
 * @param n     its size: at most `n - 1` characters are written, and a
 *              NUL after them (nothing at all if `n` is 0)
 * @param fmt   the format (see the top of this file for what it supports)
 * @param args  the arguments
 * @return how many characters were written, not counting the NUL
 */
int vsnprintf(char *s, size_t n, const char *fmt, va_list args)
{
    if (!n)
        return 0;

    /* keep room for the NUL at the end */
    n -= 1;

    size_t ret = 0;

    while (*fmt) {
        switch (*fmt) {
            case '%':
                ++fmt;
                switch (*fmt) {
                    case 'c':   /* a character */
                        ret += snputc(s + ret, n - ret, (char)va_arg(args, int));
                        break;
                    case 's':   /* a string */
                        ret += snputs(s + ret, n - ret, (char*)va_arg(args, char*));
                        break;
                    case 'd':   /* a signed decimal */
                        {
                            int32_t val = va_arg(args, int32_t);
                            if (val < 0) {
                                ret += snputc(s + ret, n - ret, '-');
                                ret += snputud(s + ret, n - ret, -(uint32_t) val);
                            } else {
                                ret += snputud(s + ret, n - ret, (uint32_t) val);
                            }
                        }
                        break;
                    case 'u':   /* an unsigned decimal */
                        ret += snputud(s + ret, n - ret, (uint32_t)va_arg(args, uint32_t));
                        break;
                    case 'l':   /* a 64-bit number */
                        switch (*++fmt) {
                            case 'x':   /* in hexadecimal */
                                ret += snputlx(s + ret, n - ret, (uint64_t)va_arg(args, uint64_t));
                                break;
                            case 'd':   /* a signed decimal */
                                {
                                    int64_t val = va_arg(args, int64_t);
                                    if (val < 0) {
                                        ret += snputc(s + ret, n - ret, '-');
                                        ret += snputul(s + ret, n - ret, -(uint64_t) val);
                                    } else {
                                        ret += snputul(s + ret, n - ret, (uint64_t) val);
                                    }
                                }
                                break;
                            case 'u':   /* an unsigned decimal */
                                ret += snputul(s + ret, n - ret, (uint64_t)va_arg(args, uint64_t));
                                break;
                            default:
                                ret += snputc(s + ret, n - ret, *--fmt);
                        }
                        break;
                    case 'p':   /* a pointer */
                        ret += snputs(s + ret, n - ret, "0x");
#if ARCH_BITS==32
                        ret += snputx(s + ret, n - ret, (uint32_t)va_arg(args, uint32_t));
#else
                        ret += snputlx(s + ret, n - ret, (uint64_t)va_arg(args, uint64_t));
#endif
                        break;
                    
                    case 'b':   /* a byte in binary */
                        ret += snputb(s + ret, n - ret, (uint8_t)(uint32_t)va_arg(args, uint32_t));
                        break;
                    case 'x':   /* hexadecimal */
                        ret += snputx(s + ret, n - ret, (uint32_t)va_arg(args, uint32_t));
                        break;
                    default:
                        ret += snputc(s + ret, n - ret, *(--fmt));
                }
                ++fmt;
                break;
            default:
                ret += snputc(s + ret, n - ret, *fmt);
                ++fmt;
        }
    }

    s[ret] = '\0';

    return ret;
}

/**
 * Formats a string into a buffer.
 *
 * @param s    the buffer
 * @param n    its size: at most `n - 1` characters are written, and a NUL
 *             after them (nothing at all if `n` is 0)
 * @param fmt  the format (see the top of this file for what it supports)
 * @return how many characters were written, not counting the NUL
 */
int snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(s, n, fmt, args);
    va_end(args);
    return ret;
}
