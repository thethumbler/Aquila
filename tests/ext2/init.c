/*
 * ext2 write path
 *
 * Mounts the ext2 image attached as the first ATA disk, writes a file
 * large enough to need singly and doubly indirect blocks (1K blocks),
 * appends to it after reopening, and reads everything back. tests/run.sh
 * then checks the image on the host with e2fsck and compares the file
 * contents with the same pattern.
 */

#include "../lib/aqtest.h"

/* > 12 direct + 256 singly indirect 1K blocks, reaches doubly indirect */
#define FIRST   300000u
#define EXTRA   5000u

static char buf[8192];

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

        int w = SC(S_WRITE, fd, buf, n);
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

    SC(S_LSEEK, fd, 0, 0);

    while (off < len) {
        u32 n = len - off > sizeof(buf) ? sizeof(buf) : len - off;
        int r = SC(S_READ, fd, buf, n);

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

    check("mknod /dev/hda", SC(S_MKNOD, "/dev/hda", S_IFBLK | 0644, DEV(3, 0)), 0);
    check("mount ext2", aq_mount("ext2", "/mnt", &args), 0);

    int fd = SC(S_OPEN, "/mnt/big", O_RDWR | O_CREAT, 0644);
    check("create file", fd >= 0, 1);
    check("write through indirect blocks", write_pattern(fd, 0, FIRST), 0);
    check("close", SC(S_CLOSE, fd, 0, 0), 0);

    /* append after reopening: used to wipe the inode block pointers */
    fd = SC(S_OPEN, "/mnt/big", O_RDWR, 0);
    check("reopen", fd >= 0, 1);
    check("seek to end", SC(S_LSEEK, fd, FIRST, 0), FIRST);
    check("append", write_pattern(fd, FIRST, EXTRA), 0);
    check("read back", verify(fd, FIRST + EXTRA), -1);
    check("close", SC(S_CLOSE, fd, 0, 0), 0);

    fd = SC(S_OPEN, "/mnt/small", O_RDWR | O_CREAT, 0644);
    check("small file", SC(S_WRITE, fd, "hello aquila\n", 13), 13);
    SC(S_CLOSE, fd, 0, 0);

    aq_end();
}
