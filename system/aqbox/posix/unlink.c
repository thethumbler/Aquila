#include <aqbox.h>
#include <unistd.h>
#include <stdio.h>

AQBOX_APPLET(unlink)(int argc, char **argv)
{
    if (argc < 2)
        return -1;

    if (unlink(argv[1])) {
        perror("unlink");
        return -1;
    }

    return 0;
}
