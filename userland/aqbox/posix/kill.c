#include <aqbox.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

static void usage()
{
    fprintf(stderr, 
            "Usage: kill [-signal_number] pid...\n"
            "Terminate or signal processes\n");
}

static int do_kill(int argc, const char *argv[], int sig)
{
    for (int i = 0; i < argc; ++i) {
        int pid;
        sscanf(argv[i], "%d", &pid);
        if (kill(pid, sig)) {
            fprintf(stderr, "kill: %s\n", strerror(errno));
        }
    }

    return 0;
}

AQBOX_APPLET(kill)(int argc, char *argv[])
{
    int sig = SIGTERM;

    if (argc < 2) {
        usage();
        return -1;
    }

    int i = 1;

    if (argv[1][0] == '-') {
        sscanf(&argv[1][1], "%d", &sig);
        ++i;
    }

    do_kill(argc - i, (const char **) &argv[i], sig);
    return 0;
}
