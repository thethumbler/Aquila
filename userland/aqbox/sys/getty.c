#include <aqbox.h>
#include <unistd.h>
#include <fcntl.h>

AQBOX_APPLET(getty)(int argc, char **argv)
{
    if (argc < 2) {
        /* should print usage */
    }

    for (int i = 0; i < 3; ++i)
        close(i);

    open(argv[1], O_RDONLY);
    open(argv[1], O_WRONLY);
    open(argv[1], O_WRONLY);

    char *argp[] = {"/sbin/login", 0};

_retry:
    //if (!fork()) {
        execve(argp[0], argp, 0);
    //}

    return 0;
}
