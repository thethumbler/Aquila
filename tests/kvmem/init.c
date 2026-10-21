/*
 * The kernel's allocator (mm/kvmem.c): what the kernel allocates for a
 * while gets back, and the self-test it ran at boot (kvmem.test) passed
 *
 * Processes forked and reaped (their kernel stacks, their tables),
 * programs executed (execve's argument buffer), threads made and joined:
 * once they're gone, the kernel has as many big objects as before
 * (/proc/meminfo's KVMemObjCnt). The other boot (kvmem.guard) is the
 * kernel's own: a write past an object's end faults (tests/run.sh).
 */

#include "../lib/aqtest.h"

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        ++a, ++b;
    return *a == *b;
}

/* /proc/meminfo's KVMemObjCnt, -1 if it isn't there */
static int kvmem_objects(void)
{
    static char buf[512];
    int fd = SC(SYS_open, "/proc/meminfo", O_RDONLY, 0);
    int n = SC(SYS_read, fd, buf, sizeof(buf) - 1);
    SC(SYS_close, fd, 0, 0);

    const char *key = "KVMemObjCnt: ";

    for (int i = 0; i < n; ++i) {
        int k = 0;

        while (key[k] && buf[i + k] == key[k])
            ++k;

        if (!key[k]) {
            int v = 0;

            for (char *c = &buf[i + k]; *c >= '0' && *c <= '9'; ++c)
                v = v * 10 + (*c - '0');

            return v;
        }
    }

    return -1;
}

static char tstack[4][8192] __attribute__((aligned(16)));
static void thr(void *u, void *a) { SC(SYS_thread_exit, 0, 0, 0); for (;;); (void) u; (void) a; }

void aq_main(void)
{
    /* executed by itself: done */
    uptr *entry = (uptr *) aq_entry_esp;
    char **argv = (char **) entry[1];

    if (entry[0] == 2 && str_eq(argv[1], "exec"))
        SC(SYS_exit, 0, 0, 0);

    aq_begin("kvmem");
    check("mount procfs", aq_mount("procfs", "/proc", 0), 0);

    int before = kvmem_objects(), st;
    check("KVMemObjCnt", before > 0, 1);

    for (int i = 0; i < 20; ++i) {
        int c = SC(SYS_fork, 0, 0, 0);
        if (!c)
            SC(SYS_exit, 0, 0, 0);
        SC(SYS_waitpid, c, &st, 0);
    }

    char *args[] = {"/init", "exec", 0};

    for (int i = 0; i < 10; ++i) {
        int c = SC(SYS_fork, 0, 0, 0);
        if (!c) {
            SC(SYS_execve, "/init", args, 0);
            SC(SYS_exit, 1, 0, 0);
        }
        SC(SYS_waitpid, c, &st, 0);
        if (st)
            break;
    }
    check("execve", st, 0);

    for (int i = 0; i < 4; ++i) {
        struct aquila_thread_args ut = {(uptr) &tstack[i][8192], (uptr) thr, 0, 0, 0};
        int tid = SC(SYS_thread_create, &ut, 0, 0);
        SC(SYS_thread_join, tid, 0, 0);
    }

    /* what's freed when a thread is switched away from has been */
    for (int i = 0; i < 10; ++i)
        SC(SYS_getpid, 0, 0, 0);
    aq_spin(10000000);

    int after = kvmem_objects();
    check("forks, execs and threads leave nothing behind", after, before);

    aq_end();
}
