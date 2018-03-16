#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>

static char *fmt = 
"\
%s@%s\n\
OS: %s\n\
Kernel: %s %s %s\n\
Uptime: %s\n\
Shell: %s %s\n\
Resolution: %s\n\
Terminal: %s\n\
Font: %s\n\
CPU: %s\n\
RAM: %dMiB / %dMiB\n\
";

void get_mem(size_t *u, size_t *t)
{
    FILE *meminfo = fopen("/proc/meminfo", "r");
    fscanf(meminfo, "MemTotal: %u kB\nMemFree: %u kB\n", t, u);
    *u = *t - *u;
    *u /= 1024;
    *t /= 1024;
    fclose(meminfo);
}

char *get_uptime()
{
    static char buf[128];
    FILE *uptime = fopen("/proc/uptime", "r");
    size_t seconds;
    fscanf(uptime, "%u", &seconds);
    fclose(uptime);

    size_t days  = (seconds / 3600) / 24;
    size_t hours = (seconds / 3600) % 24;
    size_t minutes = (seconds / 60) % 60;
    seconds = seconds % 60;

    snprintf(buf, 128, "%ud %uh %um %us", days, hours, minutes, seconds);
    return buf;
}

AQBOX_APPLET(screenfetch)(int argc, char *argv[])
{
    char *user = NULL, *node = NULL, *os = NULL, *mach = NULL,
         *kernel = NULL, *version = NULL, *uptime = NULL, *shell = NULL,
         *shell_ver = NULL, *res = NULL, *term = NULL, *font = NULL,
         *cpu = NULL;

    size_t ram_used = 0, ram_total = 0;

    struct utsname name;
    uname(&name);

    user      = getenv("USER");
    node      = name.nodename;
    os        = "AquilaOS";
    mach      = name.machine;
    kernel    = name.sysname;
    version   = name.release;
    uptime    = get_uptime();
    shell     = "aqsh";
    shell_ver = "0.0.1";
    res       = "640x480";
    term      = "fbterm";
    font      = "Ubuntu Mono 10";
    cpu       = "i686";

    get_mem(&ram_used, &ram_total);

	printf(fmt, user, node, os, mach, kernel, version, uptime, shell, shell_ver, res, term, font, cpu, ram_used, ram_total);
	return 0;
}
