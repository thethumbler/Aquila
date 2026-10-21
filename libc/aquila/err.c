/*
 * err(3): the program's name (__progname, set by crt0), the message, and
 * for err() and warn() what errno says
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char *__progname;

static void message(const char *fmt, va_list ap, int errnum)
{
    fprintf(stderr, "%s: ", __progname);

    if (fmt) {
        vfprintf(stderr, fmt, ap);
        if (errnum >= 0)
            fputs(": ", stderr);
    }

    if (errnum >= 0)
        fputs(strerror(errnum), stderr);

    fputc('\n', stderr);
}

void vwarn(const char *fmt, va_list ap)
{
    message(fmt, ap, errno);
}

void vwarnx(const char *fmt, va_list ap)
{
    message(fmt, ap, -1);
}

void verr(int status, const char *fmt, va_list ap)
{
    vwarn(fmt, ap);
    exit(status);
}

void verrx(int status, const char *fmt, va_list ap)
{
    vwarnx(fmt, ap);
    exit(status);
}

void warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwarn(fmt, ap);
    va_end(ap);
}

void warnx(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwarnx(fmt, ap);
    va_end(ap);
}

void err(int status, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    verr(status, fmt, ap);
}

void errx(int status, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    verrx(status, fmt, ap);
}
