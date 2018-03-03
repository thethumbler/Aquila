#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

AQBOX_APPLET(pwd)(int argc, char **argv)
{
    char buf[1024];
    getcwd(buf, 1024);
    printf("%s\n", buf);
    return 0;
}
