/*
 * More than one CPU (tests/run.sh boots it with 4)
 *
 * - threads run at the same time: the main thread sees the others'
 *   counters move between two of its reads, microseconds apart (one CPU
 *   would have them stand still while it runs)
 * - a page unmapped while another thread reads it, on another CPU: it
 *   faults by the time munmap returns (its TLB was flushed)
 * - fork while another thread writes, on another CPU: the child's copy
 *   doesn't change (the page became read-only on that CPU too)
 * - processes whose threads run on other CPUs, killed, or exiting
 * - a signal to a process running on another CPU, taken
 * - the FPU's state, threads moving from CPU to CPU: what each computes
 *   comes out right
 * - exec while three threads run on the other CPUs: they end first, the
 *   new image (this program again, "alone") runs by itself
 * - threads still run at the same time after all that: no CPU was left
 *   unable to take interrupts (a killed thread's)
 */

#include "../lib/aqtest.h"

#include <aquila/mman.h>

#define STACK_SIZE  16384

static char stacks[8][STACK_SIZE] __attribute__((aligned(16)));

/* a thread at `fn', on the stack `n' */
static int thread(int n, void (*fn)(void *, void *))
{
    struct aquila_thread_args ta = {(uptr) &stacks[n][STACK_SIZE], (uptr) fn, 0, 0, 0};
    return SC(SYS_thread_create, &ta, 0, 0);
}

static void __attribute__((noreturn)) thread_exit(void)
{
    SC(SYS_thread_exit, 0, 0, 0);
    for (;;);
}

static int next_id;

static int thread_id(void)
{
    return __atomic_fetch_add(&next_id, 1, __ATOMIC_SEQ_CST);
}

/*
 * At the same time: three threads count, each its own counter (its own
 * cache line); the main thread reads them all, a few microseconds apart,
 * many times: how often all three moved meanwhile, in percent
 */
static volatile int stop;
static volatile unsigned long counters[3][16];

static void counter(void *u, void *a)
{
    volatile unsigned long *c = counters[thread_id()];

    while (!stop)
        (*c)++;

    thread_exit();
}

static int at_once(void)
{
    int tids[3], moved = 0, samples = 20000;

    stop = 0;
    next_id = 0;

    for (int i = 0; i < 3; ++i)
        tids[i] = thread(i, counter);

    /* they started */
    for (int i = 0; i < 3; ++i)
        while (!counters[i][0])
            ;

    for (int s = 0; s < samples; ++s) {
        unsigned long before[3];

        for (int i = 0; i < 3; ++i)
            before[i] = counters[i][0];

        aq_spin(2000);

        int all = 1;

        for (int i = 0; i < 3; ++i)
            all = all && counters[i][0] != before[i];

        moved += all;
    }

    stop = 1;

    for (int i = 0; i < 3; ++i)
        SC(SYS_thread_join, tids[i], 0, 0);

    return moved * 100 / samples;
}

/*
 * munmap: a thread reads a page over and over, it's unmapped; its read
 * faults (its handler exits the thread) no later than munmap returns:
 * the reads it made by then are all it makes
 */
static volatile unsigned long reads, reads_at_fault;
static volatile int faulted;
static volatile int *page;

static void segv_handler(int sig)
{
    reads_at_fault = reads;
    faulted = 1;
    thread_exit();
}

static void reader(void *u, void *a)
{
    while (1) {
        (void) *page;
        reads++;
    }
}

static int unmapped_then_faults(void)
{
    struct aquila_mmap_args ma = {0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0};
    void *m;

    if (SC(SYS_mmap, &ma, &m, 0))
        return -1;

    page = m;
    *page = 1;

    struct aquila_sigaction sa = {(uptr) segv_handler, 0, 0};
    SC(SYS_sigaction, SIGSEGV, &sa, 0);

    reads = 0;
    faulted = 0;
    int tid = thread(3, reader);

    while (reads < 100000)
        ;

    SC(SYS_munmap, m, 4096, 0);
    unsigned long after = reads;

    while (!faulted)
        ;

    SC(SYS_thread_join, tid, 0, 0);

    struct aquila_sigaction dfl = {0, 0, 0};
    SC(SYS_sigaction, SIGSEGV, &dfl, 0);

    return reads_at_fault <= after;
}

/*
 * fork while a thread writes: the child's copy of what it writes stays
 * as it was at the fork
 */
static volatile unsigned long written;

static void writer(void *u, void *a)
{
    while (!stop)
        written++;

    thread_exit();
}

static int fork_while_writing(void)
{
    stop = 0;
    written = 0;
    int tid = thread(4, writer);

    while (written < 100000)
        ;

    int pid = SC(SYS_fork, 0, 0, 0);

    if (!pid) {
        unsigned long w = written;
        aq_spin(5000000);
        SC(SYS_exit, written == w ? 0 : 1, 0, 0);
    }

    int status = -1;
    SC(SYS_waitpid, pid, &status, 0);

    stop = 1;
    SC(SYS_thread_join, tid, 0, 0);

    return status;
}

/*
 * A process of four threads, all of them counting (on the other CPUs):
 * killed, or one of them exits
 */
static void spinner(void *u, void *a)
{
    for (;;)
        counters[0][0]++;
}

static void spinning_process(int how)
{
    for (int i = 0; i < 3; ++i)
        thread(i, spinner);

    if (how == 1)
        SC(SYS_exit, 3, 0, 0);

    for (;;)
        counters[1][0]++;
}

static int killed_while_running(void)
{
    int ok = 1;

    for (int round = 0; round < 10; ++round) {
        int pid = SC(SYS_fork, 0, 0, 0);

        if (!pid)
            spinning_process(0);

        aq_spin(1000000);
        SC(SYS_kill, pid, SIGKILL, 0);

        int status = -1;
        SC(SYS_waitpid, pid, &status, 0);
        ok = ok && (status & 0x7f) == SIGKILL;
    }

    return ok;
}

static int exited_while_running(void)
{
    int ok = 1;

    for (int round = 0; round < 10; ++round) {
        int pid = SC(SYS_fork, 0, 0, 0);

        if (!pid)
            spinning_process(1);

        int status = -1;
        SC(SYS_waitpid, pid, &status, 0);
        ok = ok && status == (3 << 8);
    }

    return ok;
}

/* a signal to a process that runs, on another CPU */
static void usr1_handler(int sig)
{
    SC(SYS_exit, 7, 0, 0);
}

static void spin_for_signal(void)
{
    for (;;)
        ;
}

static int signal_while_running(void)
{
    struct aquila_sigaction sa = {(uptr) usr1_handler, 0, 0};
    SC(SYS_sigaction, SIGUSR1, &sa, 0);

    int pid = SC(SYS_fork, 0, 0, 0);

    if (!pid)
        spin_for_signal();

    aq_spin(1000000);
    SC(SYS_kill, pid, SIGUSR1, 0);

    int status = -1;
    SC(SYS_waitpid, pid, &status, 0);

    struct aquila_sigaction dfl = {0, 0, 0};
    SC(SYS_sigaction, SIGUSR1, &dfl, 0);

    return status;
}

/*
 * The FPU: eight threads (more than CPUs, they move) add up their own
 * series, in floating point; the sums are exact (small integers and
 * halves), whatever was switched in between
 */
static volatile int fpu_ok[8];

static void fpu_summer(void *u, void *a)
{
    int me = thread_id();
    volatile double sum = 0, step = me + 0.5;

    for (int i = 1; i <= 2000000; ++i)
        sum += step;

    fpu_ok[me] = sum == step * 2000000;
    thread_exit();
}

static int fpu_moving(void)
{
    int tids[8], ok = 1;

    next_id = 0;

    for (int i = 0; i < 8; ++i)
        tids[i] = thread(i, fpu_summer);

    for (int i = 0; i < 8; ++i)
        SC(SYS_thread_join, tids[i], 0, 0);

    for (int i = 0; i < 8; ++i)
        ok = ok && fpu_ok[i];

    return ok;
}

/*
 * exec while threads run on the other CPUs: they write where the new
 * image checks a pattern; had one gone on (the same code), it'd change
 */
static volatile unsigned char exec_pattern[16384];

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        ++a, ++b;

    return *a == *b;
}

static void exec_alone(void)
{
    char **argv = aq_argv;

    if (aq_argc != 2 || !str_eq(argv[1], "alone"))
        return;

    for (int round = 0; round < 40; ++round) {
        for (int i = 0; i < (int) sizeof(exec_pattern); ++i)
            exec_pattern[i] = i * 7 + round;

        aq_spin(200000);

        for (int i = 0; i < (int) sizeof(exec_pattern); ++i)
            if (exec_pattern[i] != (unsigned char) (i * 7 + round))
                SC(SYS_exit, 1, 0, 0);
    }

    SC(SYS_exit, 0x55, 0, 0);
}

static void exec_writer(void *u, void *a)
{
    int me = thread_id();

    for (;;)
        exec_pattern[1000 * (me + 1)] = 1;
}

static void exec_while_running(void)
{
    next_id = 0;

    for (int i = 0; i < 3; ++i)
        thread(i, exec_writer);

    aq_spin(2000000);

    char *argv[] = {"/init", "alone", 0}, *envp[] = {0};
    SC(SYS_execve, "/init", argv, envp);
    SC(SYS_exit, 9, 0, 0);
}

void aq_main(void)
{
    exec_alone();
    aq_begin("smp");

    int pct = at_once();
    aq_puts("at once: ");
    aq_putn(pct);
    aq_puts("%\n");
    check("threads run at once", pct >= 50, 1);

    check("munmap: the reader on another CPU faults by then", unmapped_then_faults(), 1);
    check("fork: a thread's writes on another CPU aren't the child's", fork_while_writing(), 0);
    check("kill: threads running on other CPUs", killed_while_running(), 1);
    check("exit: threads running on other CPUs", exited_while_running(), 1);
    check("signal: to a process running on another CPU", signal_while_running(), 7 << 8);
    check("fpu: threads moving between CPUs", fpu_moving(), 1);
    check("exec: the threads on other CPUs end first", aq_child_status(exec_while_running), 0x55 << 8);

    pct = at_once();
    aq_puts("at once, after: ");
    aq_putn(pct);
    aq_puts("%\n");
    check("threads still run at once", pct >= 50, 1);

    aq_end();
}
