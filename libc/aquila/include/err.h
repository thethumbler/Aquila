#ifndef _ERR_H
#define _ERR_H

/* the BSDs' messages and exits: "program: message: strerror(errno)" on
 * stderr (the x ones without errno) */

#include <stdarg.h>

void err(int status, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));
void errx(int status, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));
void warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void warnx(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void verr(int status, const char *fmt, va_list ap) __attribute__((noreturn));
void verrx(int status, const char *fmt, va_list ap) __attribute__((noreturn));
void vwarn(const char *fmt, va_list ap);
void vwarnx(const char *fmt, va_list ap);

#endif /* ! _ERR_H */
