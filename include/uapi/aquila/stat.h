#ifndef _AQUILA_STAT_H
#define _AQUILA_STAT_H

#include <stdint.h>

/*
 * stat(2) result, the C library's struct stat must have the same layout
 * (checked when libc/ is built).
 */

struct aquila_timespec {
    uint64_t tv_sec;
    uint32_t tv_nsec;
} __attribute__((packed, aligned(4)));

struct aquila_stat {
    uint16_t  st_dev;
    uint16_t  st_ino;
    uint32_t  st_mode;
    uint16_t  st_nlink;
    uint32_t  st_uid;
    uint32_t  st_gid;
    uint16_t  st_rdev;
    uint32_t  st_size;

    struct aquila_timespec st_atim;
    struct aquila_timespec st_mtim;
    struct aquila_timespec st_ctim;

    uint32_t  st_blksize;
    uint32_t  st_blocks;
};

/* the C library defines these too, with the same values */
#ifndef S_IFMT
#define S_IFMT      0170000 /* type of file */
#define S_IFSOCK    0140000 /* socket */
#define S_IFLNK     0120000 /* symbolic link */
#define S_IFREG     0100000 /* regular */
#define S_IFBLK     0060000 /* block special */
#define S_IFDIR     0040000 /* directory */
#define S_IFCHR     0020000 /* character special */
#define S_IFIFO     0010000 /* fifo */

#define S_ISUID     0004000 /* set user id on execution */
#define S_ISGID     0002000 /* set group id on execution */
#define S_ISVTX     0001000 /* sticky bit */

#define S_IRWXU     0000700 /* read, write, execute: owner */
#define S_IRUSR     0000400 /* read permission, owner */
#define S_IWUSR     0000200 /* write permission, owner */
#define S_IXUSR     0000100 /* execute/search permission, owner */
#define S_IRWXG     0000070 /* read, write, execute: group */
#define S_IRGRP     0000040 /* read permission, group */
#define S_IWGRP     0000020 /* write permission, group */
#define S_IXGRP     0000010 /* execute/search permission, group */
#define S_IRWXO     0000007 /* read, write, execute: other */
#define S_IROTH     0000004 /* read permission, other */
#define S_IWOTH     0000002 /* write permission, other */
#define S_IXOTH     0000001 /* execute/search permission, other */

#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif

#endif /* ! _AQUILA_STAT_H */
