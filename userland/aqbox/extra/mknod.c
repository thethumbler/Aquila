#include <aqbox.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

void mknod_usage()
{
    fprintf(stderr, "Usage: mknod [-m MODE] NAME TYPE MAJOR MINOR\n");
}

AQBOX_APPLET(mknod)(int argc, char **argv)
{
    mode_t mode = 0666;     /* less the umask, unless -m */
    int opt;

    while ((opt = getopt(argc, argv, "m:")) != -1) {
        switch (opt) {
            case 'm':
                mode = strtoul(optarg, NULL, 8) & 07777;
                umask(0);
                break;
            default:
                mknod_usage();
                return -1;
        }
    }

    if (argc - optind != 4) {
        mknod_usage();
        return -1;
    }

    char *path  = argv[optind];
    char *type  = argv[optind + 1];
    char *major = argv[optind + 2];
    char *minor = argv[optind + 3];

    switch (type[0]) {
        case 'b': mode |= S_IFBLK; break;
        case 'c': mode |= S_IFCHR; break;
    }

    unsigned major_n, minor_n;
    sscanf(major, "%u", &major_n);
    sscanf(minor, "%u", &minor_n);

    dev_t dev = (((major_n) & 0xFF) << 8) | ((minor_n) & 0xFF);

    if (mknod(path, mode, dev)) {
        perror("mknod");
        return -1;
    }

    return 0;
}
