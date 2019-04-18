#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>

/* Flags */
#define PRINT_MACHINE   0x01
#define PRINT_NODENAME  0x02
#define PRINT_RELEASE   0x04
#define PRINT_SYSNAME   0x08
#define PRINT_VERSION   0x10

AQBOX_APPLET(uname)(int argc, char **argv)
{
    struct utsname name;
    uname(&name);

    if (argc == 1) {
        printf("%s\n", name.sysname);
    } else {
        int opt, flags = 0;
        while ((opt = getopt(argc, argv, "amnrsv")) != -1) {
            switch (opt) {
                case 'a': flags = -1; break;
                case 'm': flags |= PRINT_MACHINE; break;
                case 'n': flags |= PRINT_NODENAME; break;
                case 'r': flags |= PRINT_RELEASE; break;
                case 's': flags |= PRINT_SYSNAME; break;
                case 'v': flags |= PRINT_VERSION; break;
                default: exit(-1);
            }
        }

        if (flags & PRINT_SYSNAME)
            printf("%s ", name.sysname);
        if (flags & PRINT_NODENAME)
            printf("%s ", name.nodename);
        if (flags & PRINT_RELEASE)
            printf("%s ", name.release);
        if (flags & PRINT_VERSION)
            printf("%s ", name.version);
        if (flags & PRINT_MACHINE)
            printf("%s ", name.machine);

        printf("\n");
    }
}
