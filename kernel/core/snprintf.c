#include <core/system.h>

static int snputc(char *s, size_t n, char c)
{
    if (n) {
        *s = c;
        return 1;
    }

    return 0;
}

static int snputs(char *s, size_t n, char *str)
{
    /* a NULL string is a bug, printing it shouldn't make another one */
    if (!str)
        str = "(null)";

    unsigned i;
    for (i = 0; i < n && str[i]; ++i)
        s[i] = str[i];
    return i;
}

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
 * Formats at most n - 1 characters into s and always NUL terminates it
 * (when n > 0), returns the number of characters written to s excluding
 * the terminating NUL.
 */
int vsnprintf(char *s, size_t n, const char *fmt, va_list args)
{
    if (!n)
        return 0;

    /* Reserve space for the NUL terminator */
    n -= 1;

    size_t ret = 0;

    while (*fmt) {
        switch (*fmt) {
            case '%':
                ++fmt;
                switch (*fmt) {
                    case 'c':   /* char */
                        ret += snputc(s + ret, n - ret, (char)va_arg(args, int));
                        break;
                    case 's':   /* char * */
                        ret += snputs(s + ret, n - ret, (char*)va_arg(args, char*));
                        break;
                    case 'd': /* decimal */
                        ret += snputud(s + ret, n - ret, (uint32_t)va_arg(args, uint32_t));
                        break;
                    case 'l':   /* long */
                        switch (*++fmt) {
                            case 'x':   /* long hex */
                                ret += snputlx(s + ret, n - ret, (uint64_t)va_arg(args, uint64_t));
                                break;
                            case 'd':
                                ret += snputul(s + ret, n - ret, (uint64_t)va_arg(args, uint64_t));
                                break;
                            default:
                                ret += snputc(s + ret, n - ret, *--fmt);
                        }
                        break;
                    
                    case 'b': /* binary */
                        ret += snputb(s + ret, n - ret, (uint8_t)(uint32_t)va_arg(args, uint32_t));
                        break;
                    case 'x': /* Hexadecimal */
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

int snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(s, n, fmt, args);
    va_end(args);
    return ret;
}
