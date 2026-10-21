/*
 * Memory beyond what the kernel used to handle
 *
 * Run with more memory than 4 GiB holds on x86-64 (more than the old
 * page tracking's 3 GiB): anonymous mappings are filled until most of it
 * is used, every page with where it is, then read back. A page the
 * kernel doesn't track, or gives to two places, doesn't keep its value.
 * First one big mapping (a quarter of it, 1 GiB at most), whose pages
 * are many in one table of the kernel's.
 *
 * /size has how much to fill, in MiB (tests/run.sh writes it).
 */

#include "../lib/aqtest.h"

#include <aquila/mman.h>

#define CHUNK   (64UL << 20)    /* each its own mapping, its pages few */
#define CHUNKS  256             /* 16 GiB at most */

static unsigned long *chunk[CHUNKS];

void aq_main(void)
{
    aq_begin("memory");

    /* how much to fill, in MiB */
    char text[16] = {0};
    int fd = SC(SYS_open, "/size", O_RDONLY, 0);
    check("read /size", SC(SYS_read, fd, text, sizeof(text) - 1) > 0, 1);
    SC(SYS_close, fd, 0, 0);

    unsigned long want = 0;
    for (char *c = text; *c >= '0' && *c <= '9'; ++c)
        want = want * 10 + (*c - '0');
    want <<= 20;

    /* one big mapping, filled, read back and unmapped */
    unsigned long big = want / 4 > (1UL << 30) ? 1UL << 30 : want / 4 & ~(CHUNK - 1);
    unsigned long *p = 0;
    struct aquila_mmap_args ba = {0, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0};
    check("one big mapping", SC(SYS_mmap, &ba, &p, 0) == 0 && p, 1);

    int big_bad = 0;

    if (p) {
        for (unsigned long off = 0; off < big; off += 4096)
            p[off / sizeof(long)] = off + 1;

        for (unsigned long off = 0; off < big; off += 4096)
            if (p[off / sizeof(long)] != off + 1)
                ++big_bad;

        SC(SYS_munmap, p, big, 0);
    }

    check("its pages kept what they were given", big_bad, 0);

    int n = 0;

    for (unsigned long got = 0; got < want && n < CHUNKS; got += CHUNK) {
        void *p = 0;
        struct aquila_mmap_args a = {0, CHUNK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0};

        if (SC(SYS_mmap, &a, &p, 0) || !p)
            break;

        chunk[n] = p;

        /* each page: where it is */
        for (unsigned long off = 0; off < CHUNK; off += 4096)
            chunk[n][off / sizeof(long)] = n * CHUNK + off + 1;

        ++n;
    }

    check("mapped and filled (MiB)", (int) ((unsigned long) n * CHUNK >> 20),
          (int) (((want + CHUNK - 1) / CHUNK * CHUNK) >> 20));

    int bad = 0;

    for (int i = 0; i < n; ++i)
        for (unsigned long off = 0; off < CHUNK; off += 4096)
            if (chunk[i][off / sizeof(long)] != i * CHUNK + off + 1)
                ++bad;

    check("every page kept what it was given", bad, 0);

    aq_end();
}
