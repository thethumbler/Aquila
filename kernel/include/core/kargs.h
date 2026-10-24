#ifndef _KARGS_H
#define _KARGS_H

/* The kernel's command line (core/kargs.c). */

/* Splits the command line into arguments, at boot. */
int kargs_parse(const char *_cmdline);

/* Looks up an argument: 0 if it was given, -1 if not. */
int kargs_get(const char *key, const char **value);

#endif /* ! _KARGS_H */
