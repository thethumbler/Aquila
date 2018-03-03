#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>

AQBOX_APPLET(ls)
(int argc, char **args)
{
    DIR *d = NULL;
    int ret = 0, print_name = 0;

    if (argc == 1) {
        /* Open directory */
        char *pwd = getenv("PWD");
        if (!(d = opendir(pwd))) {
            fprintf(stderr, "ls: cannot access '%s': ", args[1]);
            perror("");
            return errno;
        }

        /* Print enteries */
        struct dirent *ent;
        while (ent = readdir(d)) {
            printf("%s\n", ent->d_name);
        }
        closedir(d);
    } else {
        print_name = argc > 2;
        for (int i = 1; i < argc; ++i) {
            print_name? printf("%s:\n", args[i]): 0;
            /* Open directory */
            DIR *d = opendir(args[i]);
            if (!(d = opendir(args[i]))) {
                fprintf(stderr, "ls: cannot access '%s': ", args[i]);
                perror("");
                ret = -1;
                continue;
            }

            /* Print enteries */
            struct dirent *ent;
            while (ent = readdir(d)) {
                printf("%s\n", ent->d_name);
            }
            closedir(d);
        }
    }

    return ret;
}
