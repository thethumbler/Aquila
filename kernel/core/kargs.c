/** \file
 * The kernel's command line: the arguments the boot loader passed, such as
 * `root=/dev/hda2` or `apic=off`.
 *
 * The command line is split once at boot into `key` or `key=value`
 * arguments, and the rest of the kernel looks them up by key.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/kargs.h>
#include <aquila/errno.h>

/* The most arguments that are kept; the rest are ignored. */
#define MAX_ARGS 128

static struct {
    const char *key;
    const char *value;
} kargs[MAX_ARGS];

static int kargc;

/**
 * Splits the kernel's command line into arguments, at boot.
 *
 * @param _cmdline  the command line, as the boot loader passed it
 * @return how many arguments there are, or `-ENOMEM` if there was no
 *         memory to split them
 */
int kargs_parse(const char *_cmdline)
{
    printk("kernel: cmdline: %s\n", _cmdline);

    if (!_cmdline || !*_cmdline)
        return 0;

    char **tokens = tokenize(_cmdline, ' ');

    /* no memory for the arguments: the kernel goes on without them */
    if (!tokens)
        return -ENOMEM;

    for (char **token_p = tokens; *token_p && kargc < MAX_ARGS; ++token_p) {
        char *token = *token_p;

        kargs[kargc].key   = token;
        kargs[kargc].value = NULL;

        /* split on the first '=' only: the value may contain '=' too */
        for (char *c = token; *c; ++c) {
            if (*c == '=') {
                *c = 0;
                kargs[kargc].value = c + 1;
                break;
            }
        }

        ++kargc;
    }

    return kargc;
}

/**
 * Looks up an argument of the kernel's command line.
 *
 * @param key    the argument's name
 * @param value  if not NULL, set to the argument's value, or to NULL if
 *               it has none (`key` alone rather than `key=value`)
 * @return 0 if the argument was given, or -1 if it wasn't
 */
int kargs_get(const char *key, const char **value)
{
    for (int i = 0; i < kargc; ++i) {
        if (!strcmp(kargs[i].key, key)) {
            if (value)
                *value = kargs[i].value;

            return 0;
        }
    }

    return -1;
}
