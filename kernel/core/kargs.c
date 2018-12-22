#include <core/system.h>
#include <core/string.h>

#define MAX_ARGS 128

static struct {
    const char *key;
    const char *value;
} kargs[MAX_ARGS] = {0};

static int kargc;

int kargs_parse(const char *_cmdline)
{
    /* TODO use hashmap */

    if (!_cmdline || !*_cmdline)
        return 0;

    char **tokens = tokenize(_cmdline, ' ');

    foreach (token, tokens) {
        int i, len = strlen(token);
        for (i = 0; i < len; ++i) {
            if (token[i] == '=') {
                token[i] = 0;
                kargs[kargc].key   = token;
                kargs[kargc].value = &token[i+1];
            }
        }

        if (i == len)
            kargs[kargc].key = token;

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
