/*
 * ubench: UnixBench's index, without Perl
 *
 * What UnixBench's Run does for the index tests: each test's program
 * runs a number of times (passes), `copies' of it at once, from a work
 * directory in /tmp; each prints COUNT|<count>|<timebase>|<unit> (and
 * maybe TIME|<seconds>) on its standard error (its standard output is
 * thrown away). A pass's score is the
 * copies' counts added up, per timebase of their average time (a rate
 * if the timebase is 0); the test's score is the geometric mean of its
 * passes', the worst third dropped. Its index is its score against the
 * baseline's (a SPARCstation 20-61, 10.0), the overall index the
 * geometric mean of the tests'.
 *
 * usage: ubench [-c copies] [-i passes] [test...]
 *
 *   -c     copies of each test at once (default 1: run it again with the
 *          number of CPUs to see what they do together)
 *   -i     passes of each test (default: 10 for the short ones, 3 for
 *          the others, as Run does)
 *
 * The tests are the index's (default: all of them, in Run's order).
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BINDIR      "/usr/lib/unixbench"
#define TESTDIR     BINDIR "/testdir"

#define PASSES_LONG     10
#define PASSES_SHORT    3
#define COPIES_MAX      64

struct test {
    const char *name;
    const char *title;
    const char *prog;
    const char *args[12];       /* after the program's name */
    int long_test;              /* 10 passes, not 3 */
    double baseline;
};

/* $UB_TMPDIR is the work directory, fstime's -d */
static const struct test tests[] = {
    {"dhry2reg", "Dhrystone 2 using register variables", "dhry2reg", {"10"}, 1, 116700},
    {"whetstone-double", "Double-Precision Whetstone", "whetstone-double", {0}, 1, 55.0},
    {"execl", "Execl Throughput", "execl", {"30"}, 0, 43.0},
    {"fstime", "File Copy 1024 bufsize 2000 maxblocks", "fstime",
     {"-c", "-t", "30", "-d", ".", "-b", "1024", "-m", "2000"}, 0, 3960},
    {"fsbuffer", "File Copy 256 bufsize 500 maxblocks", "fstime",
     {"-c", "-t", "30", "-d", ".", "-b", "256", "-m", "500"}, 0, 1655},
    {"fsdisk", "File Copy 4096 bufsize 8000 maxblocks", "fstime",
     {"-c", "-t", "30", "-d", ".", "-b", "4096", "-m", "8000"}, 0, 5800},
    {"pipe", "Pipe Throughput", "pipe", {"10"}, 1, 12440},
    {"context1", "Pipe-based Context Switching", "context1", {"10"}, 1, 4000},
    {"spawn", "Process Creation", "spawn", {"30"}, 0, 126},
    {"shell1", "Shell Scripts (1 concurrent)", "looper", {"60", BINDIR "/multi.sh", "1"}, 0, 42.4},
    {"shell8", "Shell Scripts (8 concurrent)", "looper", {"60", BINDIR "/multi.sh", "8"}, 0, 6},
    {"syscall", "System Call Overhead", "syscall", {"10"}, 1, 15000},
};

#define NTESTS  (sizeof(tests) / sizeof(tests[0]))

struct result {
    double score;
    char unit[16];
    int passes;
    double seconds;     /* a pass's, on average */
};

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* one copy's output: COUNT and TIME */
struct copy {
    pid_t pid;
    int fd;
    double start, end;
    double count, timebase, time;
    char unit[16];
    int status, counted;
    char line[256];             /* the line being read */
    size_t len;
    char last[256];             /* the last line, if it failed */
};

/* a line of a copy's output: COUNT and TIME are taken */
static void parse(struct copy *c, char *line)
{
    if (!strncmp(line, "COUNT|", 6)) {
        char *p = line + 6, *q;
        c->count = strtod(p, &q);

        if (*q == '|') {
            c->timebase = strtod(q + 1, &q);

            if (*q == '|')
                snprintf(c->unit, sizeof(c->unit), "%s", q + 1);
        }

        c->counted = 1;
    } else if (!strncmp(line, "TIME|", 5)) {
        c->time = strtod(line + 5, NULL);
    } else if (*line) {
        snprintf(c->last, sizeof(c->last), "%s", line);
    }
}

/* what a copy wrote, a line at a time (a long one cut) */
static void feed(struct copy *c, const char *buf, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (buf[i] == '\n' || c->len == sizeof(c->line) - 1) {
            c->line[c->len] = '\0';
            parse(c, c->line);
            c->len = 0;

            if (buf[i] == '\n')
                continue;
        }

        c->line[c->len++] = buf[i];
    }
}

/* a pass: `copies' of the test at once; its score, 0 if one failed */
static double pass(const struct test *t, int copies, char *unit, double *seconds)
{
    static struct copy cs[COPIES_MAX];
    char prog[256];
    const char *argv[16];
    int argc = 0;

    snprintf(prog, sizeof(prog), "%s/%s", BINDIR, t->prog);
    argv[argc++] = prog;

    for (int i = 0; t->args[i]; ++i)
        argv[argc++] = t->args[i];

    argv[argc] = NULL;

    for (int i = 0; i < copies; ++i) {
        struct copy *c = &cs[i];
        int p[2];

        memset(c, 0, sizeof(*c));

        if (pipe(p)) {
            perror("ubench: pipe");
            return 0;
        }

        c->start = now();
        c->pid = fork();

        if (c->pid < 0) {
            perror("ubench: fork");
            return 0;
        }

        /* the results are on its standard error; its standard output
         * (Whetstone's table) would cut in the middle of their lines */
        if (!c->pid) {
            int null = open("/dev/null", O_WRONLY);

            close(p[0]);
            dup2(null, 1);
            dup2(p[1], 2);
            close(null);
            close(p[1]);
            execv(prog, (char **) argv);
            fprintf(stderr, "ubench: %s: %s\n", prog, strerror(errno));
            _exit(127);
        }

        close(p[1]);
        c->fd = p[0];
    }

    /* their outputs, read as they come (a test may write more than a pipe
     * holds: Whetstone's table): a copy's ends at its end, then it's
     * waited for */
    struct pollfd fds[COPIES_MAX];
    int open_fds = copies;

    for (int i = 0; i < copies; ++i)
        fds[i] = (struct pollfd) {cs[i].fd, POLLIN, 0};

    while (open_fds) {
        if (poll(fds, copies, -1) < 0) {
            if (errno == EINTR)
                continue;

            perror("ubench: poll");
            return 0;
        }

        for (int i = 0; i < copies; ++i) {
            if (fds[i].fd < 0 || !fds[i].revents)
                continue;

            char buf[1024];
            ssize_t n = read(fds[i].fd, buf, sizeof(buf));

            if (n > 0) {
                feed(&cs[i], buf, n);
                continue;
            }

            feed(&cs[i], "\n", 1);
            cs[i].end = now();
            close(fds[i].fd);
            fds[i].fd = -1;
            open_fds--;
        }
    }

    double count = 0, time = 0;

    for (int i = 0; i < copies; ++i) {
        struct copy *c = &cs[i];

        waitpid(c->pid, &c->status, 0);

        if (!c->counted || c->status) {
            fprintf(stderr, "ubench: %s failed (status %d): %s\n", t->name, c->status, c->last);
            return 0;
        }

        count += c->count;
        time += c->time > 0 ? c->time : c->end - c->start;
    }

    time /= copies;
    *seconds = time;
    snprintf(unit, 16, "%s", cs[0].unit);

    /* a rate (timebase 0), or counts per timebase */
    return cs[0].timebase > 0 && time > 0 ? count / (time / cs[0].timebase) : count;
}

static int by_score(const void *a, const void *b)
{
    double x = *(const double *) a, y = *(const double *) b;
    return x < y ? -1 : x > y;
}

/* the test's passes: the worst third dropped, the geometric mean of the
 * others; 0 if one failed */
static int run(const struct test *t, int copies, int passes, struct result *r)
{
    double scores[64], seconds = 0;
    int n = passes ? passes : t->long_test ? PASSES_LONG : PASSES_SHORT;

    if (n > 64)
        n = 64;

    printf("%-40s", t->title);
    fflush(stdout);

    for (int i = 0; i < n; ++i) {
        double s;

        if (!(scores[i] = pass(t, copies, r->unit, &s)))
            return 0;

        seconds += s;
        printf(".");
        fflush(stdout);
    }

    qsort(scores, n, sizeof(scores[0]), by_score);

    double logs = 0;
    int kept = 0;

    for (int i = n / 3; i < n; ++i, ++kept)
        logs += log(scores[i]);

    r->score = exp(logs / kept);
    r->passes = kept;
    r->seconds = seconds / n;

    printf("%*s%14.1f %-5s (%.1f s, %d samples)\n", 12 - n, "", r->score, r->unit,
           r->seconds, r->passes);
    return 1;
}

/* `from' copied to `to' */
static int copy(const char *from, const char *to)
{
    char buf[8192];
    ssize_t n;
    int in = open(from, O_RDONLY), out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    while (in >= 0 && out >= 0 && (n = read(in, buf, sizeof(buf))) > 0)
        if (write(out, buf, n) != n)
            n = -1, in = -1;

    int err = in < 0 || out < 0 || n < 0;
    close(in);
    close(out);
    return err;
}

/* the work directory: shell scripts sort sort.src in it, fstime writes
 * its files in it */
static char *workdir(void)
{
    static char dir[] = "/tmp/ubench.XXXXXX";
    char path[64];

    if (!mkdtemp(dir)) {
        perror("ubench: /tmp");
        return NULL;
    }

    snprintf(path, sizeof(path), "%s/sort.src", dir);

    if (copy(TESTDIR "/sort.src", path) || chdir(dir)) {
        fprintf(stderr, "ubench: can't set up %s: %s\n", dir, strerror(errno));
        return NULL;
    }

    setenv("UB_BINDIR", BINDIR, 1);
    setenv("UB_TMPDIR", dir, 1);
    setenv("UB_TESTDIR", dir, 1);
    return dir;
}

/* the work directory goes, what the tests left in it too */
static void workdir_remove(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char path[320];

    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        unlink(path);
    }

    if (d)
        closedir(d);

    chdir("/");
    rmdir(dir);
}

static void usage(void)
{
    fprintf(stderr, "usage: ubench [-c copies] [-i passes] [test...]\ntests:");

    for (size_t i = 0; i < NTESTS; ++i)
        fprintf(stderr, " %s", tests[i].name);

    fprintf(stderr, "\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int copies = 1, passes = 0, opt;

    while ((opt = getopt(argc, argv, "c:i:h")) != -1) {
        switch (opt) {
            case 'c': copies = atoi(optarg); break;
            case 'i': passes = atoi(optarg); break;
            default: usage();
        }
    }

    if (copies < 1 || copies > COPIES_MAX || passes < 0)
        usage();

    int chosen[NTESTS] = {0}, any = 0;

    for (int i = optind; i < argc; ++i) {
        size_t j;

        for (j = 0; j < NTESTS && strcmp(argv[i], tests[j].name); ++j)
            ;

        if (j == NTESTS)
            usage();

        chosen[j] = any = 1;
    }

    char *dir = workdir();

    if (!dir)
        return 1;

    printf("UnixBench %s (ubench), %d %s of each test\n\n", "6.0.1", copies,
           copies == 1 ? "copy" : "copies");

    struct result results[NTESTS];
    int ran[NTESTS] = {0}, failed = 0;

    for (size_t i = 0; i < NTESTS; ++i) {
        if (any && !chosen[i])
            continue;

        if (run(&tests[i], copies, passes, &results[i]))
            ran[i] = 1;
        else
            failed = 1;
    }

    printf("\n%-40s%14s %12s %9s\n", "System Benchmarks Index Values", "BASELINE", "RESULT", "INDEX");

    double logs = 0;
    int n = 0;

    for (size_t i = 0; i < NTESTS; ++i) {
        if (!ran[i])
            continue;

        double index = results[i].score / tests[i].baseline * 10;
        printf("%-40s%14.1f %12.1f %9.1f\n", tests[i].title, tests[i].baseline,
               results[i].score, index);
        logs += log(index);
        n++;
    }

    if (n) {
        printf("%-40s%14s %12s %9s\n", "", "", "", "========");
        printf("%-40s%14s %12s %9.1f%s\n", "System Benchmarks Index Score", "", "",
               exp(logs / n), n < (int) NTESTS ? " (partial)" : "");
    }

    workdir_remove(dir);
    return failed;
}
