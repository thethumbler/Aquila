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

#define USE_FBTERM  1

void _start()
{
#if USE_FBTERM
    struct {char *dev; char *opt} data;
    data.dev = "/dev/hda1";
    mount("ext2", "/", 0, &data);
    mount("devfs", "/dev", 0, 0);
    //mkdir("/dev/pts", 0);
    mount("devpts", "/dev/pts", 0, 0);

    if (!fork())
        execve("/bin/fbterm", 0, 0);
#else
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

		char *argp[] = {"/bin/aqbox", "sh", 0};
		char *envp[] = {"PWD=/", 0};
		execve("/bin/aqbox", argp, envp);
	}
#endif

	for (;;);
}
