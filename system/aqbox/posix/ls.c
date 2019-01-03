#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

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

char *file_mode(struct stat *buf)
{
    char a_t;
    switch (buf->st_mode & S_IFMT) { 
        case S_IFDIR:  a_t = 'd'; break;
        case S_IFCHR:  a_t = 'c'; break;
        case S_IFBLK:  a_t = 'b'; break;
        case S_IFREG:  a_t = '-'; break;
        case S_IFLNK:  a_t = 'l'; break;
        case S_IFSOCK: a_t = 'x'; break;
        case S_IFIFO:  a_t = 'p'; break;
    };

    static char mode[11];

    mode[0] = a_t; 
    mode[1] = buf->st_mode & S_IRUSR? 'r' : '-';
    mode[2] = buf->st_mode & S_IWUSR? 'w' : '-';
    mode[3] = buf->st_mode & S_IXUSR? 'x' : '-';
    mode[4] = buf->st_mode & S_IRGRP? 'r' : '-';
    mode[5] = buf->st_mode & S_IWGRP? 'w' : '-';
    mode[6] = buf->st_mode & S_IXGRP? 'x' : '-';
    mode[7] = buf->st_mode & S_IROTH? 'r' : '-';
    mode[8] = buf->st_mode & S_IWOTH? 'w' : '-';
    mode[9] = buf->st_mode & S_IXOTH? 'x' : '-';
    mode[10] = '\0';

    return mode;
}

int print_long_entry(char *path, char *fn)
{
    char full_path[512];
    snprintf(full_path, 512, "%s/%s", path, fn);

    struct stat buf = {0};

    if (lstat(full_path, &buf)) {
        fprintf(stderr, "stat: cannot stat '%s': ", fn);
        perror("");
        return -1;
    }

    /* <file mode> */
    char *mode = file_mode(&buf);

    /* <number of links> */
    unsigned nlink = buf.st_nlink;

    /* <owner name> */
    struct passwd *passwd = getpwuid(buf.st_uid);
    char *owner = passwd? passwd->pw_name : "";

    /* <group name> */
    char *group = "root";

    /* <device info> */
    char device[16] = "";
    dev_t dev = buf.st_rdev;

    if (dev)
        snprintf(device, 16, "%u, %u", (dev >> 8) & 0xFF, dev & 0xFF);

    /* <size> */
    size_t size = buf.st_size;

    /* <date and time> */
    char time[64] = "";
    struct tm *tm = gmtime(&buf.st_mtim.tv_sec);
    strftime(time, 64, "%b %e %Y %H:%M", tm);
    
    /* <pathname> */
    char name[512];

    if (S_ISLNK(buf.st_mode)) {
        int fd = open(full_path, O_RDONLY | O_NOFOLLOW);
        char symlink[512];
        symlink[0] = 0;
        read(fd, symlink, 512);
        close(fd);
        snprintf(name, 512, "%s -> %s", fn, symlink);
    } else {
        snprintf(name, 512, "%s", fn);
    }

    if (S_ISCHR(buf.st_mode) || S_ISBLK(buf.st_mode))
        printf("%s %u %s %s %8s %s %s\n", mode, nlink, owner, group, device, time, name);
    else
        printf("%s %u %s %s %8u %s %s\n", mode, nlink, owner, group, size, time, name);

    return 0;
}

int do_ls(char *path, uint32_t flags)
{
    /* Open directory */
    DIR *dir = NULL;

    if (!(dir = opendir(path))) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", path, strerror(errno));
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

    size_t maxlen = 0;
    for (int i = 0; i < entries_idx + 1; ++i) {
        size_t len = strlen(entries[i].d_name);
        if (len > maxlen)
            maxlen = len;
    }

    size_t termwidth = 80;
    size_t entries_per_line = termwidth / (maxlen + 2);

    /* print entries */
    if (flags & FLAG_l) {   /* long mode */
        for (int i = 0; i < entries_idx + 1; ++i) {
            if ((flags & FLAG_a) || (entries[i].d_name[0] != '.'))
                print_long_entry(path, entries[i].d_name);
        }
    } else {
        int j = 0;
        for (int i = 0; i < entries_idx + 1; ++i) {
            if ((flags & FLAG_a) || (entries[i].d_name[0] != '.')) {
                printf("%-*s  ", maxlen, entries[i].d_name);
                fflush(stdout);
                ++j;

                if (j == entries_per_line) {
                    j = 0;
                    printf("\n");
                    fflush(stdout);
                }
            }
        }

        if (j != 0) {
            printf("\n");
            fflush(stdout);
        }
    }

    closedir(dir);
}

int ls_usage()
{
}

AQBOX_APPLET(ls)(int argc, char **argv)
{
    int print_name = 0;
    uint32_t flags = 0;

    int pathc = 0;
    char *pathv[argc];

    int opt;

    while ((opt = getopt(argc, argv, "al")) != -1) {
        switch (opt) {
            case 'a': flags |= FLAG_a; break;
            case 'l': flags |= FLAG_l; break;
            default: ls_usage(); exit(-1);
        }
    }

    while (optind < argc)
        pathv[pathc++] = argv[optind++];

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
