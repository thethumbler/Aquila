#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void truncate_usage()
{
    fprintf(stderr, "Usage: truncate OPTION... FILE...\n");
}

#define FLAG_c  0x01
#define FLAG_o  0x02
#define FLAG_r  0x04
#define FLAG_s  0x08

AQBOX_APPLET(truncate)(int argc, char **argv)
{
    int opt, flags = 0;
    const char *ref = NULL;
    int size = 0;

    while ((opt = getopt(argc, argv, "cor:s:")) != -1) {
        switch (opt) {
            case 'c': flags |= FLAG_c; break;
            case 'o': flags |= FLAG_o; break;
            case 'r': flags |= FLAG_r; ref = optarg; break;
            case 's': flags |= FLAG_s; size = atoi(optarg); break;
            default: truncate_usage(); return -1;
        }
    }

    if (flags & ((FLAG_r | FLAG_s) == 0)) {
        fprintf(stderr, "truncate: you must specify either '-s' or '-r'\n");
        return -1;
    }

    if (optind >= argc) {
        fprintf(stderr, "truncate: missing file operand\n");
        return -1;
    }

    for (opt = optind; opt < argc; ++opt) {
        if (truncate(argv[opt], size)) {
            perror("truncate");
            return -1;
        }
    }

    return 0;
}
