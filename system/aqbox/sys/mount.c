#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>

void usage(char *name)
{
    fprintf(stderr, "usage: %s -t fstype [-o options] [dev] dir", name);
    exit(-1);
}

AQBOX_APPLET(mount)(int argc, char **argv)
{
    // mount -t fstype [-o options] [dev] dir
    char *type = NULL, *opt = NULL, *dev  = NULL, *dir  = NULL;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 't':
                    if (i < argc - 1) type = argv[++i];
                    else usage(argv[0]);
                    break;
                case 'o':
                    if (i < argc - 1) opt  = argv[++i];
                    else usage(argv[0]);
                    break;
                default:
                    fprintf(stderr, "Unrecognized option %s\n", argv[i]);
                    return -1;
            }
            continue;
        }

        if (dev) dir = argv[i];
        else     dev = argv[i];

    }

    if (!type) {
        fprintf(stderr, "Filesystem type must be supplied\n");
        return -1;
    }

    if (dev && !dir) {
        dir = dev;
        dev = NULL;
    }

    if (!dir) {
        fprintf(stderr, "Directory must be supplied\n");
        return -1;
    }

    struct {
        char *dev;
        char *opt;
    } data = {dev, opt};

    //printf("mount -t %s -o %s %s %s\n", type, opt, dev, dir);
    mount(type, dir, 0, &data);
    
    return 0;
}
