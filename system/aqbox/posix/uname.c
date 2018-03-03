#include <aqbox.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>

/* Flags */
#define PRINT_MACHINE   0x1
#define PRINT_NODENAME  0x2
#define PRINT_RELEASE   0x4
#define PRINT_SYSNAME   0x8
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
