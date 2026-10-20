#include <aqbox.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

static void usage()
{
    fprintf(stderr, 
            "Usage: mkdir [-m mode] dir...\n"
            "Make directories\n");
}

AQBOX_APPLET(mkdir)(int argc, char **argv)
{
    int opt;
    unsigned mode = 0775;

    while ((opt = getopt(argc, argv, "hm:")) != -1) {
        switch (opt) {
            case 'm':
                sscanf(optarg, "%o", &mode);
                break;
            case 'h':
                usage();
                return 0;
        }
    }

    for (int i = optind; i < argc; ++i) {
        if (mkdir(argv[i], mode)) {
            perror("mkdir");
            return -1;
        }
    }

    return 0;
}
