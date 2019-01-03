#include <aqbox.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#ifndef CAT_BUFSIZE
#define CAT_BUFSIZE 512
#endif

static void usage()
{
    fprintf(stderr, 
            "Usage: cat [-u] [file...]\n"
            "Concatenate and print files\n");
}

char buf[CAT_BUFSIZE];
static int do_cat(const char *path)
{
    int err = 0;
    int fd = 0; /* Use stdin by default */

    if (path) {
        fd = open(path, O_RDONLY);

        if (fd < 0) {
            fprintf(stderr, "cat: %s: %s\n", path, strerror(errno));
            return -1;
        }
    }


    int r;
    while ((r = read(fd, buf, CAT_BUFSIZE)) > 0) {
        if ((err = write(1, buf, r)) < 0) {
            fprintf(stderr, "cat: write: %s\n", strerror(errno));
            goto error;
        }
    }

    if (r < 0) {
        fprintf(stderr, "cat: read: %s\n", strerror(errno));
        err = r;
        goto error;
    }

error:
    close(fd);
    return err;
}

AQBOX_APPLET(cat)(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "uh")) != -1) {
        switch (opt) {
            case 'u':
                /* ignore */
                break;
            case 'h':
                usage();
                return 0;
            default:
                usage();
                return -1;
        }
    }

    for (int i = optind; i < argc; ++i) {
        if (!strcmp(argv[i], "-"))
            do_cat(NULL);
        else
            do_cat(argv[i]);
    }
}
