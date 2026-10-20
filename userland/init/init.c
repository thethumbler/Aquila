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

int _log(int level, const char *fmt, ...)
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
        _log(1, "failed to execute %s: %s\n", argv[0], strerror(errno));
        exit(x);
    }

    return status;
}

/* login on a terminal, again whenever the session ends */
void spawn_login(const char *tty, const char *term)
{
    if (fork())
        return;

    for (int i = 0; i < 3; ++i)
        close(i);

    /* a session of its own, the terminal becomes its controlling
     * terminal when it is opened */
    setsid();
    setenv("TERM", term, 1);

    /* read-write as usual, programs may write to standard input */
    if (open(tty, O_RDWR) < 0) {
        _log(1, "init: can't open %s: %s\n", tty, strerror(errno));
        exit(1);
    }

    dup(0);
    dup(0);

    char *argp[] = {"/sbin/login", NULL};
    while (1) run(argp);
}

void signal_handler(int sig)
{
    switch (sig) {
        case SIGTERM:
            _log(1, "init: reached target reboot\n");
            exit(0);
        default:
            /* ignore */
            return;
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
        _log(1, "could not open stderr\n");
    }

    _log(1, "init: started\n");

    /* execute /etc/rc.init script */
    _log(1, "init: executing /etc/rc.init script\n");
    char *argp[] = {"/bin/sh", "/etc/rc.init", NULL};
    run(argp);

    int use_serial = 0;

    if (argc > 0) {
        char *cmdline = argv[0], *token;
        while ((token = strtok(cmdline, " "))) {
            cmdline = NULL;
            if (!strcmp(token, "--serial"))
                use_serial = 1;
        }
    }

    /* logins on the first virtual terminals, and the serial port */
    spawn_login("/dev/tty1", "linux");
    spawn_login("/dev/tty2", "linux");
    spawn_login("/dev/tty3", "linux");

    /* whatever is on the other end, vt100 is the safe guess */
    if (use_serial)
        spawn_login("/dev/ttyS0", "vt100");

    /* init can't die */
	for (;;);
}
