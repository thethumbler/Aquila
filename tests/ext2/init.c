/*
 * ext2 write path
 *
 * Mounts the ext2 image attached as the first ATA disk, writes a file
 * large enough to need singly and doubly indirect blocks (1K blocks),
 * appends to it after reopening, and reads everything back. Then
 * directories that grow past a block, removing, renaming and linking,
 * and truncating; the disk has no partition table: its first partition
 * isn't there (it isn't the whole disk). tests/run.sh then checks the image on the host with
 * e2fsck, compares the file contents with the same pattern and lists
 * what is left.
 */

#include "../lib/aqtest.h"

#include <aquila/dirent.h>

/* > 12 direct + 256 singly indirect 1K blocks, reaches doubly indirect */
#define FIRST   300000u
#define EXTRA   5000u

static char buf[8192];
static char st[128];

#define ST ((struct aquila_stat *) st)

/* "<prefix>NN" */
static char *name(const char *prefix, int n)
{
    static char s[32];
    int i = 0;

    while (*prefix)
        s[i++] = *prefix++;

    s[i++] = '0' + n / 10;
    s[i++] = '0' + n % 10;
    s[i] = 0;
    return s;
}

/* number of entries of a directory, without . and .. */
static int entries(const char *path)
{
    struct dirent d;
    int fd = SC(SYS_open, path, O_RDONLY, 0), n = 0;

    while (SC(SYS_readdir, fd, &d, 0) > 0)
        n += !(d.d_name[0] == '.' && (!d.d_name[1] || (d.d_name[1] == '.' && !d.d_name[2])));

    SC(SYS_close, fd, 0, 0);
    return n;
}

/* must match the pattern in tests/run.sh */
static unsigned char pat(u32 off)
{
    return (unsigned char) (off * 7 + (off >> 9));
}

static int write_pattern(int fd, u32 start, u32 len)
{
    while (len) {
        u32 n = len > sizeof(buf) ? sizeof(buf) : len;

        for (u32 i = 0; i < n; ++i)
            buf[i] = pat(start + i);

        int w = SC(SYS_write, fd, buf, n);
        if (w != (int) n)
            return w < 0 ? w : -1;

        start += n;
        len   -= n;
    }

    return 0;
}

/* return the offset of the first mismatch, or -1 if all bytes match */
static int verify(int fd, u32 len)
{
    u32 off = 0;

    SC(SYS_lseek, fd, 0, 0);

    while (off < len) {
        u32 n = len - off > sizeof(buf) ? sizeof(buf) : len - off;
        int r = SC(SYS_read, fd, buf, n);

        if (r <= 0)
            return off;

        for (int i = 0; i < r; ++i)
            if ((unsigned char) buf[i] != pat(off + i))
                return off + i;

        off += r;
    }

    return -1;
}

void aq_main(void)
{
    struct { char *dev, *opt; } args = {"/dev/hda", 0};

    aq_begin("ext2");

    check("mknod /dev/hda", SC(SYS_mknod, "/dev/hda", S_IFBLK | 0644, DEV(3, 0)), 0);
    check("mount ext2", aq_mount("ext2", "/mnt", &args), 0);

    int fd = SC(SYS_open, "/mnt/big", O_RDWR | O_CREAT, 0644);
    check("create file", fd >= 0, 1);
    check("write through indirect blocks", write_pattern(fd, 0, FIRST), 0);
    check("close", SC(SYS_close, fd, 0, 0), 0);

    /* append after reopening: used to wipe the inode block pointers */
    fd = SC(SYS_open, "/mnt/big", O_RDWR, 0);
    check("reopen", fd >= 0, 1);
    check("seek to end", SC(SYS_lseek, fd, FIRST, 0), FIRST);
    check("append", write_pattern(fd, FIRST, EXTRA), 0);
    check("read back", verify(fd, FIRST + EXTRA), -1);
    check("close", SC(SYS_close, fd, 0, 0), 0);

    fd = SC(SYS_open, "/mnt/small", O_RDWR | O_CREAT, 0644);
    check("small file", SC(SYS_write, fd, "hello aquila\n", 13), 13);
    SC(SYS_close, fd, 0, 0);

    /* a directory that needs more than one block (16-byte entries) */
    SC(SYS_stat, "/mnt", st, 0);
    int root_links = ST->st_nlink;
    check("mkdir", SC(SYS_mkdir, "/mnt/d", 0755, 0), 0);
    SC(SYS_stat, "/mnt", st, 0);
    check("parent links +1", ST->st_nlink, root_links + 1);
    int made = 0;
    for (int i = 0; i < 99; ++i) {
        char path[32] = "/mnt/d/";
        char *n = name("file-number-", i);
        for (int j = 0; n[j]; ++j) path[7 + j] = n[j];
        fd = SC(SYS_open, path, O_CREAT | O_WRONLY, 0644);
        made += fd >= 0;
        SC(SYS_close, fd, 0, 0);
    }
    check("99 files", made, 99);
    check("all listed", entries("/mnt/d"), 99);
    SC(SYS_stat, "/mnt/d", st, 0);
    check("directory grew", ST->st_size > 1024, 1);
    check("rmdir not empty", SC(SYS_rmdir, "/mnt/d", 0, 0), -ENOTEMPTY);
    int removed = 0;
    for (int i = 0; i < 99; i += 2) {
        char path[32] = "/mnt/d/";
        char *n = name("file-number-", i);
        for (int j = 0; n[j]; ++j) path[7 + j] = n[j];
        removed += SC(SYS_unlink, path, 0, 0) == 0;
    }
    check("unlinked 50", removed, 50);
    check("49 left", entries("/mnt/d"), 49);
    check("removed one gone", SC(SYS_stat, "/mnt/d/file-number-00", st, 0), -ENOENT);
    check("kept one there", SC(SYS_stat, "/mnt/d/file-number-01", st, 0), 0);
    for (int i = 1; i < 99; i += 2) {
        char path[32] = "/mnt/d/";
        char *n = name("file-number-", i);
        for (int j = 0; n[j]; ++j) path[7 + j] = n[j];
        SC(SYS_unlink, path, 0, 0);
    }
    check("empty again", entries("/mnt/d"), 0);
    check("rmdir", SC(SYS_rmdir, "/mnt/d", 0, 0), 0);
    SC(SYS_stat, "/mnt", st, 0);
    check("parent links back", ST->st_nlink, root_links);

    /* rename, also of a directory to another parent */
    SC(SYS_mkdir, "/mnt/a", 0755, 0);
    SC(SYS_mkdir, "/mnt/b", 0755, 0);
    SC(SYS_mkdir, "/mnt/a/sub", 0755, 0);
    fd = SC(SYS_open, "/mnt/a/sub/f", O_CREAT | O_WRONLY, 0644);
    SC(SYS_write, fd, "moved", 5);
    SC(SYS_close, fd, 0, 0);
    check("rename a directory", SC5(SYS_renameat, AT_FDCWD, "/mnt/a/sub", AT_FDCWD, "/mnt/b/sub", 0), 0);
    check("moved with its contents", SC(SYS_stat, "/mnt/b/sub/f", st, 0), 0);
    check(".. follows", SC(SYS_stat, "/mnt/b/sub/../sub/f", st, 0), 0);
    SC(SYS_stat, "/mnt/a", st, 0);
    check("old parent links", ST->st_nlink, 2);
    SC(SYS_stat, "/mnt/b", st, 0);
    check("new parent links", ST->st_nlink, 3);
    check("rename a file", SC5(SYS_renameat, AT_FDCWD, "/mnt/b/sub/f", AT_FDCWD, "/mnt/renamed", 0), 0);
    fd = SC(SYS_open, "/mnt/small", O_RDONLY, 0);
    SC(SYS_close, fd, 0, 0);
    check("rename over a file", SC5(SYS_renameat, AT_FDCWD, "/mnt/renamed", AT_FDCWD, "/mnt/small", 0), 0);
    SC(SYS_stat, "/mnt/small", st, 0);
    check("replaced", ST->st_size, 5);
    check("rmdir moved", SC(SYS_rmdir, "/mnt/b/sub", 0, 0), 0);

    /* links */
    check("link", SC(SYS_link, "/mnt/small", "/mnt/a/hard", 0), 0);
    SC(SYS_stat, "/mnt/small", st, 0);
    check("2 links", ST->st_nlink, 2);
    check("unlink one name", SC(SYS_unlink, "/mnt/small", 0, 0), 0);
    SC(SYS_stat, "/mnt/a/hard", st, 0);
    check("1 link", ST->st_nlink, 1);
    check("symlink", SC(SYS_symlinkat, "a/hard", AT_FDCWD, "/mnt/sym"), 0);
    char target[16] = {0};
    check("readlink", SC5(SYS_readlinkat, AT_FDCWD, "/mnt/sym", target, sizeof(target), 0), 6);
    fd = SC(SYS_open, "/mnt/sym", O_RDONLY, 0);
    check("read through symlink", SC(SYS_read, fd, buf, 16), 5);
    SC(SYS_close, fd, 0, 0);

    /* an unlinked file stays readable while open */
    fd = SC(SYS_open, "/mnt/gone", O_CREAT | O_RDWR, 0644);
    write_pattern(fd, 0, 20000);
    check("unlink open file", SC(SYS_unlink, "/mnt/gone", 0, 0), 0);
    check("still readable", verify(fd, 20000), -1);
    SC(SYS_close, fd, 0, 0);

    /* truncating frees blocks, growing again reads zeros */
    fd = SC(SYS_open, "/mnt/trunc", O_CREAT | O_RDWR, 0644);
    write_pattern(fd, 0, 100000);
    check("truncate", SC(SYS_ftruncate, fd, 1000, 0), 0);
    SC(SYS_fstat, fd, st, 0);
    check("size after truncate", ST->st_size, 1000);
    check("kept part", verify(fd, 1000), -1);
    SC(SYS_ftruncate, fd, 3000, 0);
    SC(SYS_lseek, fd, 1000, 0);
    int zeros = SC(SYS_read, fd, buf, 2000) == 2000;
    for (int i = 0; i < 2000; ++i)
        zeros &= buf[i] == 0;
    check("grown part reads zeros", zeros, 1);
    SC(SYS_close, fd, 0, 0);

    /* no partition table: no first partition (not the whole disk from
     * sector 0) */
    SC(SYS_mknod, "/dev/hda1", S_IFBLK | 0644, DEV(3, 1));
    fd = SC(SYS_open, "/dev/hda1", O_RDONLY, 0);
    check("a partition the disk doesn't have", fd >= 0 ? SC(SYS_read, fd, buf, 512) : fd, -ENXIO);
    SC(SYS_close, fd, 0, 0);

    /* the disk has what was written (the host checks it) */
    check("sync", SC(SYS_sync, 0, 0, 0), 0);

    aq_end();
}
