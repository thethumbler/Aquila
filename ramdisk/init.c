#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define TIOCGPTN	0x80045430
#define EAGAIN			11

void _start()
{
    fork();
    fork();
    fork();

    for (;;);
    
    int pty = open("/dev/ptmx", O_RDWR);
    int pts_id;
    ioctl(pty, TIOCGPTN, &pts_id);
    char pts_fn[] = "/dev/pts/ ";
    pts_fn[9] = '0' + pts_id;

    int kbd_pid;
    int aqsh_pid;

    if (kbd_pid = fork()) {
        int console = open("/dev/console", O_WRONLY);
        for (;;) {
            char buf[50];
            memset(buf, 0, 50);
            if (read(pty, buf, 50) > 0) {
                dprintf(console, buf);
            }
        }
    } else {
		close(pty);

        if (aqsh_pid = fork()) {
            int stdin_fd  = open("/dev/kbd", O_RDONLY);
            int stdout_fd = open(pts_fn, O_WRONLY);
            for (;;);
            execve("/bin/kbd", 0, 0);
        } else {
            int stdin_fd  = open(pts_fn, O_RDONLY);
            int stdout_fd = open(pts_fn, O_WRONLY);
            int stderr_fd = open(pts_fn, O_WRONLY);

            char *argp[] = {"ABC", "DEF", 0};
            char *envp[] = {"PWD=/", 0};
            for (;;);
            execve("/bin/aqsh", argp, envp);
        }
    }
}

#if 0
void _start()
{
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
            execve("/bin/kbd", 0, 0);
        }
	} else {
		int pts_id;
		ioctl(pty, TIOCGPTN, &pts_id);
		char pts_fn[] = "/dev/pts/ ";
		pts_fn[9] = '0' + pts_id;
		close(pty);

		int stdin_fd  = open(pts_fn, O_RDONLY);
		int stdout_fd = open(pts_fn, O_WRONLY);
		int stderr_fd = open(pts_fn, O_WRONLY);

		char *argp[] = {"ABC", "DEF", 0};
		char *envp[] = {"PWD=/", 0};
		execve("/bin/aqsh", argp, envp);
	}

	for (;;);
}
#endif
