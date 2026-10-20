#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static void usage()
{
    fprintf(stderr, "Usage: mktemp [-d] TEMPLATE\n");
}

AQBOX_APPLET(mktemp)(int argc, char **argv)
{
    const char *dir = "/tmp";
    char fn[] = "tmp.XXXXXX";
    int fd;

    if ((fd = mkstemp(fn)) == -1) {
        fprintf(stderr, "mkstemp: %s\n", strerror(errno));
        return -1;
    }

    printf("%s\n", fn);
    close(fd);
    return 0;
}
