#include <aqbox.h>
#include <sys/stat.h>

AQBOX_APPLET(mkdir)(int argc, char **argv)
{
    if (argc < 2)
        return 0;

    if (mkdir(argv[1], 0777)) {
        perror("mkdir");
        return -1;
    }

    return 0;
}
