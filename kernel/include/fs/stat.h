#ifndef _FS_STAT_H
#define _FS_STAT_H

#include <core/system.h>

struct  stat;

#include <core/types.h>

/**
 * \ingroup vfs
 */
struct  stat {
    uint16_t  st_dev;
    uint16_t  st_ino;
    uint32_t  st_mode;
    uint16_t  st_nlink;
    uint32_t  st_uid;
    uint32_t  st_gid;
    uint16_t  st_rdev;
    uint32_t  st_size;
    
    struct timespec  st_atime;
    struct timespec  st_mtime;
    struct timespec  st_ctime;
    
    uint32_t   st_blksize;
    uint32_t   st_blocks;
};

#define S_IFMT      0170000UL /* type of file */
#define S_IFSOCK    0140000UL /* socket */
#define S_IFLNK     0120000UL /* symbolic link */
#define S_IFREG     0100000UL /* regular */
#define S_IFBLK     0060000UL /* block special */
#define S_IFDIR     0040000UL /* directory */
#define S_IFCHR     0020000UL /* character special */
#define S_IFIFO     0010000UL /* fifo */

#define S_ENFMT     0002000UL /* enforcement-mode locking */
#define S_ISUID     0004000UL /* set user id on execution */
#define S_ISGID     0002000UL /* set group id on execution */
#define S_ISVTX     0001000UL /* sticky bit */

#define S_IREAD     0000400UL /* read permission, owner */
#define S_IWRITE    0000200UL /* write permission, owner */
#define S_IEXEC     0000100UL /* execute/search permission, owner */

#define S_IRUSR     0000400 /* read permission, owner */
#define S_IWUSR     0000200 /* write permission, owner */
#define S_IXUSR     0000100 /* execute/search permission, owner */
#define S_IRWXU     (S_IRUSR | S_IWUSR | S_IXUSR)

#define S_IRGRP     0000040 /* read permission, group */
#define S_IWGRP     0000020 /* write permission, grougroup */
#define S_IXGRP     0000010 /* execute/search permission, group */
#define S_IRWXG     (S_IRGRP | S_IWGRP | S_IXGRP)

#define S_IROTH     0000004 /* read permission, other */
#define S_IWOTH     0000002 /* write permission, other */
#define S_IXOTH     0000001 /* execute/search permission, other */
#define S_IRWXO     (S_IROTH | S_IWOTH | S_IXOTH)

#define S_ISSOCK(n) (((n) & S_IFMT) == S_IFSOCK)
#define S_ISLNK(n)  (((n) & S_IFMT) == S_IFLNK)
#define S_ISREG(n)  (((n) & S_IFMT) == S_IFREG)
#define S_ISBLK(n)  (((n) & S_IFMT) == S_IFBLK)
#define S_ISDIR(n)  (((n) & S_IFMT) == S_IFDIR)
#define S_ISCHR(n)  (((n) & S_IFMT) == S_IFCHR)
#define S_ISIFO(n)  (((n) & S_IFMT) == S_IFIFO)

#endif /* ! _FS_STAT_H */
