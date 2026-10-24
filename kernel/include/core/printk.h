#ifndef _PRINTK_H
#define _PRINTK_H

#include <stdarg.h>

/* The kernel's messages (core/printk.c). */

/* The levels of a subsystem's log messages (LOGGER_DEFINE()), the most
 * important first: a message is printed if its level is at most the
 * subsystem's. */
#define LOG_NONE    0
#define LOG_EMERG   1
#define LOG_ALERT   2
#define LOG_CRIT    3
#define LOG_ERR     4
#define LOG_WARNING 5
#define LOG_NOTICE  6
#define LOG_INFO    7
#define LOG_DEBUG   8

/* Writes a kernel message. */
int vprintk(const char *fmt, va_list args);
int printk(const char *fmt, ...);

/* Defines `name(level, fmt, ...)`, a logging function for a subsystem
 * that prints the messages of `level` up to `_level`, prefixed with the
 * subsystem's name. */
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

/* Declares a logging function that LOGGER_DEFINE() defines elsewhere. */
#define LOGGER_DECLARE(name) \
int name(int level, const char *fmt, ...);

#endif /* ! _PRINTK_H */
