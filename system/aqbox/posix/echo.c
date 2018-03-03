#include <aqbox.h>
#include <stdio.h>
#include <string.h>

AQBOX_APPLET(echo)(int argc, char *argv[])
{
    int newline = 1, escape = 0, argv_i = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            /* XXX Sanity checking */
            switch (argv[i][1]) {
                case 'n':
                    newline = 0;
                    break;
                case 'e':
                    escape = 1;
                    break;
            }
        } else {
            argv_i = i;
            break;
        }
    }

    for (int i = argv_i; i < argc; ++i) {
        if (escape) {
            char *s = argv[i];
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

    if (newline)
        printf("\n");

    return 0;
}
