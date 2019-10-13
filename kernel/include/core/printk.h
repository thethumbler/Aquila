#ifndef _PRINTK_H
#define _PRINTK_H

#include <stdarg.h>

#define LOG_NONE    0
#define LOG_EMERG   1
#define LOG_ALERT   2
#define LOG_CRIT    3
#define LOG_ERR     4
#define LOG_WARNING 5
#define LOG_NOTICE  6
#define LOG_INFO    7
#define LOG_DEBUG   8

int vprintk(const char *fmt, va_list args);
int printk(const char *fmt, ...);

#define LOGGER_DEFINE(module, name, _level) \
int name(int level, const char *fmt, ...) \
{ \
    if (level <= _level) { \
        va_list args; \
        va_start(args, fmt); \
        printk("%s: ", #module); \
        vprintk(fmt, args); \
        va_end(args); \
    } \
    return 0; \
}

#define LOGGER_DECLARE(name) \
int name(int level, const char *fmt, ...);

#endif /* ! _PRINTK_H */
