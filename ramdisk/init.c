#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mount.h>

#include <sys/ioctl.h>

#define TIOCGPTN	0x80045430
#define EAGAIN			11

#define USE_FBTERM   1
#define USE_CONSOLE  0
#define USE_SERIAL   0

#define PATH "/bin:/sbin"

int run_prog(char *name, char **argv)
{
    int cld;
    if (cld = fork()) {
        int s, pid;
        do {
            pid = waitpid(cld, &s, 0);
        } while (pid != cld);
    } else {
        int x = execve(name, argv, NULL);
        exit(x);
    }
}

void parse_initrc()
{
    FILE *initrc = fopen("/init.rc", "r");

    char line[1024];

    for (;;) {
        memset(line, 0, sizeof(line));

        if (!fgets(line, 1024, initrc))
            break;

        /* Ignore empty lines and comments */
        if (strlen(line) < 2 || line[0] == '#')
            continue;

        size_t llen = strlen(line);

        if (line[llen-1] == '\n')
            line[llen-1] = 0;

        /* Tokenize line */
        char *argv[50];
        memset(argv, 0, sizeof(argv));

        int args_i = 0;

        char *tok = strtok(line, " \t\n");

        while (tok) {
            argv[args_i++] = tok;
            tok = strtok(NULL, " \t\n");
        }

        char path[1024];
        strcpy(path, PATH);

        char *comp = strtok(path, ":");
        char buf[1024];

        do {
            snprintf(buf, 1024, "%s/%s", comp, argv[0]);

            int fd = open(buf, O_RDONLY);
            if (fd > 0) {
                close(fd);
                int ret =  run_prog(buf, argv);
                break;
            }
        } while (comp = strtok(NULL, ":"));
    }

    fclose(initrc);
}

void _start()
{
    parse_initrc();

#if USE_SERIAL
    open("/dev/ttyS0", O_RDONLY);
    open("/dev/ttyS0", O_WRONLY);
    open("/dev/ttyS0", O_WRONLY);

    char *argp[] = {"/sbin/login", 0};
    execve("/sbin/login", argp, 0);
#endif

#if USE_FBTERM
    if (!fork())
        execve("/bin/fbterm", 0, 0);

    for (;;);
#endif

#if USE_CONSOLE
	int pty = open("/dev/ptmx", O_RDWR);
	int pid = fork();

	if (pid) {	/* Parent */
        int kbd_proc = fork();

        if (kbd_proc) {
            int console = open("/dev/console", O_WRONLY);

            char buf[50];
            for (;;) {
                memset(buf, 0, 50);
                if (read(pty, buf, 50) > 0)
                    dprintf(console, "%s", buf);
            }
        } else {
            open("/dev/kbd", O_RDONLY);
            execve("/bin/kbd", 0, 0);
        }
	} else {
		int pts_id = 0;
		ioctl(pty, TIOCGPTN, &pts_id);
		char pts_fn[] = "/dev/pts/ ";
		pts_fn[9] = '0' + pts_id;
		close(pty);

		int stdin_fd  = open(pts_fn, O_RDONLY);
		int stdout_fd = open(pts_fn, O_WRONLY);
		int stderr_fd = open(pts_fn, O_WRONLY);

		char *argp[] = {"/sbin/login", 0};
		execve("/sbin/login", argp, 0);
	}
#endif

	for (;;);
}
