#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>

int run(char **argv)
{
    int child, status, pid;

    if (child = fork()) {
        do pid = waitpid(child, &status, 0);
        while (pid != child);
    } else {
        int x = execv(argv[0], argv);
        exit(x);
    }

    return status;
}

int main(int argc, char **argv)
{
    /* execute /etc/rc.init script */
    char *argp[] = {"/bin/sh", "/etc/rc.init", NULL};
    run(argp);

    int use_fbterm = 0, use_serial = 0;

    if (argc > 0) {
        char *cmdline = argv[0], *token;
        while ((token = strtok(cmdline, " "))) {
            cmdline = NULL;
            if (!strcmp(token, "--serial"))
                use_serial = 1;
            else if (!strcmp(token, "--fbterm"))
                use_fbterm = 1;
        }
    }

    if (use_fbterm || !use_serial) {
        char *argp[] = {"/bin/fbterm", NULL};
        if (!fork()) execv(argp[0], argp);
    }

    if (use_serial) {
        open("/dev/ttyS0", O_RDONLY);
        open("/dev/ttyS0", O_WRONLY);
        open("/dev/ttyS0", O_WRONLY);

        char *argp[] = {"/sbin/login", NULL};
        if (!fork()) execv(argp[0], argp);
    }

    /* init can't die */
	for (;;);
}
