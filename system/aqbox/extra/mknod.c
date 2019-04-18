#include <aqbox.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

void mknod_usage()
{
    fprintf(stderr, "Usage: mknod NAME TYPE MAJOR MINOR\n");
}

AQBOX_APPLET(mknod)(int argc, char **argv)
{
    if (argc != 5) {
        mknod_usage();
        return -1;
    }

    char *path  = argv[1];
    char *type  = argv[2];
    char *major = argv[3];
    char *minor = argv[4];

    umask(0660);
    mode_t mode = umask(0660);

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
