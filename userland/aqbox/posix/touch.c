#include <aqbox.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static void usage()
{
    fprintf(stderr, 
            "Usage: touch file...\n"
            "Change file access and modification times\n");
}

static int do_touch(int argc, const char *argv[], int flags)
{
    for (int i = 0; i < argc; ++i) {
        int fd;
        if ((fd = open(argv[i], O_RDONLY)) > 0) {
            /* TODO */
            close(fd);
        } else {
            fd = creat(argv[i], S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);

            if (fd < 0) {
                fprintf(stderr, "touch: creat: %s\n", strerror(errno));
                return -1;
            }
        }
    }

    return 0;
}

AQBOX_APPLET(touch)(int argc, char *argv[])
{
    int opt, flags = 0;

    while ((opt = getopt(argc, argv, "")) != -1) {
    }

    do_touch(argc - optind, (const char **) &argv[optind], flags);

    return 0;
}
