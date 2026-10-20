#include <aqbox.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/reboot.h>

/*
 * reboot [-f], poweroff [-f]: runit stops the services and restarts or
 * powers off (runit-init 6 and 0), -f does it right away.
 */
static int shutdown(int argc, char **argv, int cmd, char *level)
{
    if (argc > 1 && !strcmp(argv[1], "-f")) {
        sync();
        reboot(cmd);
        perror(argv[0]);
        return 1;
    }

    execl("/sbin/runit-init", "init", level, (char *) NULL);
    perror("/sbin/runit-init");
    return 1;
}

AQBOX_APPLET(reboot)(int argc, char **argv)
{
    return shutdown(argc, argv, RB_AUTOBOOT, "6");
}

AQBOX_APPLET(poweroff)(int argc, char **argv)
{
    return shutdown(argc, argv, RB_POWER_OFF, "0");
}
