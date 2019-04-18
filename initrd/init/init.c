#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>

int log_fd = -1;
int log_init()
{
    log_fd = open("/dev/kmsg", O_WRONLY);
}

int log(int level, const char *fmt, ...)
{
    static char buf[512];

    if (log_fd >= 0) {
        va_list ap;
        va_start(ap, fmt);
        int len = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        write(log_fd, buf, len);
    }
}

int run(char **argv)
{
    int child, status, pid;

    if (child = fork()) {
        do pid = waitpid(child, &status, 0);
        while (pid != child);
    } else {
        int x = execv(argv[0], argv);
        log(1, "failed to execute %s: %s\n", argv[0], strerror(errno));
        exit(x);
    }

    return status;
}

int signal_handler(int sig)
{
    switch (sig) {
        case SIGTERM:
            log(1, "init: reached target reboot\n");
            exit(0);
        default:
            /* ignore */
            return 0;
    }
}

int main(int argc, char **argv)
{
    signal(SIGKILL, signal_handler);
    signal(SIGTERM, signal_handler);

    close(0); close(1); close(2);
    open("/dev/kmsg", O_RDONLY);
    log_init();

    if (open("/dev/kmsg", O_WRONLY) != 2) {
        log(1, "could not open stderr\n");
    }

    log(1, "init: started\n");

    /* execute /etc/rc.init script */
    log(1, "init: executing /etc/rc.init script\n");
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
        log(1, "init: starting fbterm\n");
        char *argp[] = {"/bin/fbterm", NULL};
        if (!fork()) {
            for (int i = 0; i < 3; ++i)
                close(i);

            execv(argp[0], argp);
        }
    }

    if (use_serial) {
        if (!fork()) {
            for (int i = 0; i < 3; ++i)
                close(i);

            open("/dev/ttyS0", O_RDONLY);
            open("/dev/ttyS0", O_WRONLY);
            open("/dev/ttyS0", O_WRONLY);

            char *argp[] = {"/sbin/login", NULL};
            while (1) run(argp);
            //execv(argp[0], argp);
        }
    }

    /* init can't die */
	for (;;);
}
