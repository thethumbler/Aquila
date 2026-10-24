/*
 * Out of memory: an error, not a panic
 *
 * Run with little memory (tests/run.sh: 128 MiB). A child maps far more
 * than there is and touches it all: the page fault that finds no memory
 * kills it (SIGKILL), the system goes on, the memory comes back. Then the
 * same from inside a system call: read() from /dev/zero into such a
 * mapping, the kernel's copy to it faulting the pages in. Each three
 * times, and init (this program) forks and runs as before after them.
 */

#include "../lib/aqtest.h"

#include <aquila/mman.h>

#define HOG     (1UL << 30)     /* 1 GiB: far more than the machine has */

static void mount_proc(void)
{
    static struct { char *dev, *opt; } no_args = {0, 0};
    SC(SYS_mkdir, "/proc", 0755, 0);
    aq_mount("procfs", "/proc", &no_args);
}

/* /proc/meminfo's MemFree, in KiB */
static long memfree(void)
{
    static char text[512];
    int fd = SC(SYS_open, "/proc/meminfo", O_RDONLY, 0);
    int n = fd < 0 ? 0 : SC(SYS_read, fd, text, sizeof(text) - 1);
    SC(SYS_close, fd, 0, 0);

    for (int i = 0; i + 8 < n; ++i) {
        if (text[i] == 'M' && text[i + 3] == 'F' && text[i + 7] == ':') {
            long kb = 0;
            for (int j = i + 8; j < n && (text[j] == ' ' || (text[j] >= '0' && text[j] <= '9')); ++j)
                if (text[j] != ' ')
                    kb = kb * 10 + text[j] - '0';
            return kb;
        }
    }

    return -1;
}

static char *hog_map(void)
{
    char *p = 0;
    struct aquila_mmap_args a = {0, HOG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0};

    if (SC(SYS_mmap, &a, &p, 0) || !p)
        SC(SYS_exit, 1, 0, 0);

    return p;
}

/* every page written: it runs out in a page fault from user mode */
static void hog_touch(void)
{
    char *p = hog_map();

    for (unsigned long off = 0; off < HOG; off += 4096)
        p[off] = 1;

    SC(SYS_exit, 2, 0, 0);     /* it all fit: not out of memory */
}

/* read() fills it: it runs out in the kernel, copying to user memory */
static void hog_read(void)
{
    char *p = hog_map();
    int fd = SC(SYS_open, "/dev/zero", O_RDONLY, 0);

    if (fd < 0)
        SC(SYS_exit, 3, 0, 0);

    for (unsigned long off = 0; off < HOG; off += 1UL << 20)
        SC(SYS_read, fd, p + off, 1UL << 20);

    SC(SYS_exit, 2, 0, 0);
}

static void child_exits_7(void)
{
    SC(SYS_exit, 7, 0, 0);
}

void aq_main(void)
{
    aq_begin("oom");
    mount_proc();

    SC(SYS_mknod, "/dev/zero", S_IFCHR | 0666, DEV(1, 5));

    long before = memfree();
    check("MemFree", before > 0, 1);

    for (int i = 0; i < 3; ++i) {
        int status = aq_child_status(hog_touch);
        check("out of memory in a page fault: killed (SIGKILL)", status & 0x7F, SIGKILL);

        /* what it had is free again (a little may stay in the kernel's
         * caches: its slabs) */
        check("its memory back", memfree() > before - 2048, 1);
    }

    for (int i = 0; i < 3; ++i) {
        int status = aq_child_status(hog_read);
        check("out of memory in read(): killed (SIGKILL)", status & 0x7F, SIGKILL);
        check("its memory back", memfree() > before - 2048, 1);
    }

    check("fork and exit, after", aq_child_status(child_exits_7), 7 << 8);

    aq_end();
}
