#include <aqbox.h>
#include <signal.h>

AQBOX_APPLET(reboot)(int argc, char **argv)
{
    return kill(1, SIGTERM);
}
