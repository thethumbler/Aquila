#include <aqbox.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FLAG_n  0x0001
#define FLAG_e  0x0002

static void usage()
{
    fprintf(stderr, 
            "Usage: echo [-ne] [string...]\n"
            "Write arguments to standard output.\n");
}

static int do_echo(int argc, const char *argv[], int flags)
{
    for (int i = 0; i < argc; ++i) {
        if (flags & FLAG_e) {
            const char *s = argv[i];
            int len = strlen(s);
            for (int j = 0; j < len; ++j) {
                if (s[j] == '\\') {
                    /* XXX Sanity checking */
                    switch (s[++j]) {
                        case 'e':
                            putchar('\033');
                            break;
                        case 'n':
                            putchar('\n');
                            break;
                    }
                } else {
                    putchar(s[j]);
                }
            }
        } else {
            printf("%s ", argv[i]);
        }
    }

    if (!(flags & FLAG_n))
        printf("\n");
}

AQBOX_APPLET(echo)(int argc, char *argv[])
{
    int newline = 1, escape = 0, flags = 0, opt;

    while ((opt = getopt(argc, argv, "ne")) != -1) {
        switch (opt) {
            case 'n':
                newline = 0;
                flags  |= FLAG_n;
                break;
            case 'e':
                escape = 1;
                flags  |= FLAG_e;
                break;
        }
    }

    do_echo(argc - optind, (const char **) &argv[optind], flags);

    return 0;
}
