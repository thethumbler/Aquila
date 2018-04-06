#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>

#define FLAG_A  0x0000001
#define FLAG_C  0x0000002
#define FLAG_F  0x0000004
#define FLAG_H  0x0000008
#define FLAG_L  0x0000010
#define FLAG_R  0x0000020
#define FLAG_S  0x0000040
#define FLAG_a  0x0000080
#define FLAG_c  0x0000100
#define FLAG_d  0x0000200
#define FLAG_f  0x0000400
#define FLAG_g  0x0000800
#define FLAG_i  0x0001000
#define FLAG_k  0x0002000
#define FLAG_l  0x0004000
#define FLAG_m  0x0008000
#define FLAG_n  0x0010000
#define FLAG_o  0x0020000
#define FLAG_p  0x0040000
#define FLAG_q  0x0080000
#define FLAG_r  0x0100000
#define FLAG_s  0x0200000
#define FLAG_t  0x0400000
#define FLAG_u  0x0800000
#define FLAG_x  0x1000000

#define MAX_ENTRIES 1024
struct dirent entries[MAX_ENTRIES];

int do_ls(char *path, uint32_t flags)
{
    /* Open directory */
    DIR *dir = NULL;

    if (!(dir = opendir(path))) {
        fprintf(stderr, "ls: cannot access '%s': ", path);
        perror("");
        return -1;
    }

    int entries_idx;

    /* fetch enteries */
    for (entries_idx = -1; entries_idx < MAX_ENTRIES - 1;) {
        struct dirent *entry;

        if ((entry = readdir(dir))) {
            memcpy(&entries[++entries_idx], entry, sizeof(struct dirent));
        } else {
            break;
        }
    }

    /* sort entries - TODO */

    /* print entries */
    if (flags & FLAG_l) {   /* long mode */
        for (int i = 0; i < entries_idx + 1; ++i) {
            printf("%s\n", entries[i].d_name);
        }
    } else {
        for (int i = 0; i < entries_idx + 1; ++i) {
            printf("%s\n", entries[i].d_name);
        }
    }

    closedir(dir);
}

void parse_arg(char *arg, uint32_t *flags)
{
    size_t len = strlen(arg);
    for (int l = 0; l < len; ++l) {
        switch (arg[l]) {
            case 'l': *flags |= FLAG_l; break;
        }
    }
}

AQBOX_APPLET(ls)(int argc, char **argv)
{
    int print_name = 0;
    uint32_t flags = 0;

    int pathc = 0;
    char **pathv[argc];

    /* parse arguments */
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {    /* argument */
            parse_arg(&argv[i][1], &flags);
        } else {    /* path */
            pathv[pathc++] = argv[i];
        }
    }

    if (pathc == 0) {
        do_ls(".", flags);
    } else {
        print_name = pathc > 2;
        for (int i = 0; i < pathc; ++i) {
            print_name? printf("%s:\n", pathv[i]): 0;
            do_ls(pathv[i], flags);
        }
    }

    return 0;
}
