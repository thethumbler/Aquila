/*
 * minix (v1, 1K zones)
 *
 * Mounts the minix image attached as the first ATA disk (mkfs.minix -1),
 * writes a file large enough to need the singly and doubly indirect zones
 * and reads it back; a sparse file, its holes (a direct zone, a doubly
 * indirect one) reading zeros; truncating, which frees the zones past
 * the end (and the rest of the last one reads zeros when the file grows
 * again). tests/run.sh then checks the image on the host with fsck.minix.
 */

#include "../lib/aqtest.h"

/* > 7 direct + 512 singly indirect 1K zones: doubly indirect */
#define BIG     700000u

/* a zone in the doubly indirect ones, in a sparse file */
#define FAR     (2u << 20)

static char buf[8192];
static char st[128];

#define ST ((struct aquila_stat *) st)

static unsigned char pat(u32 off)
{
    return (unsigned char) (off * 13 + (off >> 10));
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

/* `len' bytes from `start' are the pattern's: -1, else where they aren't */
static int verify(int fd, u32 start, u32 len)
{
    u32 off = 0;

    SC(SYS_lseek, fd, start, 0);

    while (off < len) {
        u32 n = len - off > sizeof(buf) ? sizeof(buf) : len - off;
        int r = SC(SYS_read, fd, buf, n);

        if (r <= 0)
            return off;

        for (int i = 0; i < r; ++i)
            if ((unsigned char) buf[i] != pat(start + off + i))
                return off + i;

        off += r;
    }

    return -1;
}

/* `len' bytes from `start' are zeros */
static int zeros(int fd, u32 start, u32 len)
{
    SC(SYS_lseek, fd, start, 0);

    if (len > sizeof(buf) || SC(SYS_read, fd, buf, len) != (int) len)
        return 0;

    for (u32 i = 0; i < len; ++i)
        if (buf[i])
            return 0;

    return 1;
}

void aq_main(void)
{
    aq_begin("minix");

    struct { char *dev, *opt; } args = {"/dev/hda", 0};

    check("mknod /dev/hda", SC(SYS_mknod, "/dev/hda", S_IFBLK | 0644, DEV(3, 0)), 0);
    check("mount minix", aq_mount("minix", "/mnt", &args), 0);

    /* device nodes keep their numbers */
    check("mknod on minix", SC(SYS_mknod, "/mnt/devnode", S_IFCHR | 0644, DEV(4, 64)), 0);
    SC(SYS_stat, "/mnt/devnode", st, 0);
    check("device node's number", ST->st_rdev, DEV(4, 64));

    /* through the singly and doubly indirect zones */
    int fd = SC(SYS_open, "/mnt/big", O_CREAT | O_RDWR, 0644);
    check("create a file", fd >= 0, 1);
    check("write through the indirect zones", write_pattern(fd, 0, BIG), 0);
    check("read back", verify(fd, 0, BIG), -1);
    SC(SYS_close, fd, 0, 0);

    /* sparse: holes in a direct zone and before a doubly indirect one */
    fd = SC(SYS_open, "/mnt/sparse", O_CREAT | O_RDWR, 0644);
    SC(SYS_lseek, fd, 5000, 0);
    write_pattern(fd, 5000, 100);
    SC(SYS_lseek, fd, FAR, 0);
    check("write past a hole, doubly indirect", write_pattern(fd, FAR, 3000), 0);
    check("a direct zone's hole reads zeros", zeros(fd, 0, 4096), 1);
    check("the data after it", verify(fd, 5000, 100), -1);
    check("the hole before reads zeros", zeros(fd, FAR - 4096, 4096), 1);
    check("the far data", verify(fd, FAR, 3000), -1);
    SC(SYS_close, fd, 0, 0);

    /* truncating frees the zones past the end, the rest of the last one
     * reads zeros when the file grows again */
    fd = SC(SYS_open, "/mnt/big", O_RDWR, 0);
    check("truncate", SC(SYS_ftruncate, fd, 1000, 0), 0);
    SC(SYS_fstat, fd, st, 0);
    check("size after truncate", ST->st_size, 1000);
    check("kept part", verify(fd, 0, 1000), -1);
    SC(SYS_ftruncate, fd, 3000, 0);
    check("grown part reads zeros", zeros(fd, 1000, 2000), 1);
    SC(SYS_close, fd, 0, 0);

    fd = SC(SYS_open, "/mnt/sparse", O_RDWR, 0);
    check("truncate the sparse file", SC(SYS_ftruncate, fd, 0, 0), 0);
    SC(SYS_close, fd, 0, 0);

    /* the disk has what was written (the host checks it) */
    check("sync", SC(SYS_sync, 0, 0, 0), 0);

    aq_end();
}
