#include <aqbox.h>
#include <stdio.h>

extern char **environ;

AQBOX_APPLET(env)(int argc, char *argv[])
{
    char **e = environ;
    while (*e) {
        printf("%s\n", *e++);
    }
}
