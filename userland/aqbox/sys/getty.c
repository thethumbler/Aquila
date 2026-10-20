#include <aqbox.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * getty tty [term]: login on a terminal, which becomes the controlling
 * terminal of a session of its own. For runit services.
 */
AQBOX_APPLET(getty)(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: getty tty [term]\n");
        return 1;
    }

    setsid();

    for (int i = 0; i < 3; ++i)
        close(i);

    /* read-write as usual, programs may write to standard input */
    if (open(argv[1], O_RDWR) != 0)
        return 1;

    dup(0);
    dup(0);

    if (argc > 2)
        setenv("TERM", argv[2], 1);

    execl("/sbin/login", "login", (char *) NULL);
    return 1;
}
