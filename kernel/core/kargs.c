#include <core/system.h>
#include <core/string.h>
#include <core/kargs.h>

#define MAX_ARGS 128

static struct {
    const char *key;
    const char *value;
} kargs[MAX_ARGS];

static int kargc;

int kargs_parse(const char *_cmdline)
{
    printk("kernel: cmdline: %s\n", _cmdline);

    /* TODO use hashmap */

    if (!_cmdline || !*_cmdline)
        return 0;

    char **tokens = tokenize(_cmdline, ' ');

    for (char **token_p = tokens; *token_p && kargc < MAX_ARGS; ++token_p) {
        char *token = *token_p;

        kargs[kargc].key   = token;
        kargs[kargc].value = NULL;

        /* split on the first '=' only, value may itself contain '=' */
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
