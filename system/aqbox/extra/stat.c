#include <aqbox.h>
#include <stdio.h>
#include <sys/stat.h>

AQBOX_APPLET(stat)(int argc, char *argv[])
{
    if (argc < 2) {
        return -1;
    }

    const char *path = argv[1];

    struct stat buf = {0};
    lstat(path, &buf);

    char *type = NULL, a_t;
    switch (buf.st_mode & S_IFMT) { 
        case S_IFDIR:  a_t = 'd'; type = "directory"; break;
        case S_IFCHR:  a_t = 'c'; type = "character special file"; break;
        case S_IFBLK:  a_t = 'b'; type = "block special file"; break;
        case S_IFREG:  a_t = '-'; type = "regular file"; break;
        case S_IFLNK:  a_t = 'l'; type = "symbolic link"; break;
        case S_IFSOCK: a_t = 'x'; type = "socket"; break;
        case S_IFIFO:  a_t = 'p'; type = "fifo"; break;
    };

    char access[] = {
        a_t, 
        buf.st_mode & S_IRUSR? 'r' : '-',
        buf.st_mode & S_IWUSR? 'w' : '-',
        buf.st_mode & S_IXUSR? 'x' : '-',
        buf.st_mode & S_IRGRP? 'r' : '-',
        buf.st_mode & S_IWGRP? 'w' : '-',
        buf.st_mode & S_IXGRP? 'x' : '-',
        buf.st_mode & S_IROTH? 'r' : '-',
        buf.st_mode & S_IWOTH? 'w' : '-',
        buf.st_mode & S_IXOTH? 'x' : '-',
        '\0'
    };

    printf("  File: %s\n", path);
    printf("  Type: %s\n", type);
    printf("Device: %x/%x\n", buf.st_dev >> 8, buf.st_dev & 0xff);
    printf(" Inode: %d\n", buf.st_ino);
    printf("Access: %s\n", access);
    printf(" Links: %d\n", buf.st_nlink);
    printf("   UID: %d\n", buf.st_uid);
    printf("   GID: %d\n", buf.st_gid);
    printf("  rDev: %x/%x\n", buf.st_rdev >> 8, buf.st_rdev & 0xff);
    printf("  Size: %d\n", buf.st_size);

	return 0;
}
