/*
 * C library on the kernel
 *
 * Built with i686-aquila-gcc against newlib (unlike the other tests),
 * checks that what programs see through libc matches the kernel: errno
 * values, signal numbers, struct stat, and a few library functions that
 * depend on the port (stdio, getline, opendir, pipes, fork/wait, sockets,
 * vectored I/O, the interval timer, addresses, pseudo-terminals, random
 * bytes).
 *
 * Reports in the aqtest format on the serial port.
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <crypt.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <pwd.h>
#include <shadow.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

static FILE *out;
static int fails;

static void check(const char *name, long got, long want)
{
    fprintf(out, "%s %s got=%ld want=%ld\n", got == want ? "PASS" : "FAIL", name, got, want);
    if (got != want)
        ++fails;
}

static volatile sig_atomic_t got_signal;
static sigjmp_buf fault_jmp;
static void *volatile fault_addr;

/* SA_SIGINFO: the faulting address, then back to the test */
static void fault_handler(int sig, siginfo_t *info, void *ctx)
{
    fault_addr = info->si_addr;
    siglongjmp(fault_jmp, sig);
}

static void handler(int sig)
{
    got_signal = sig;
}

/* virtual terminal 2 (VGA text mode, 80x25 under QEMU -kernel) */
static volatile int alarms;
static void alarm_handler(int sig) { alarms++; (void) sig; }

/* what the libc port adds for X: vectored I/O, addresses, socket calls,
 * the interval timer */
static void test_x_support(void)
{
    /* writev: one write, readv: one read spread over the vectors */
    int p[2];
    pipe(p);
    struct iovec wv[3] = {{"ab", 2}, {"", 0}, {"cde", 3}};
    check("writev", writev(p[1], wv, 3), 5);
    char r1[2], r2[8] = {0};
    struct iovec rv[2] = {{r1, 2}, {r2, sizeof(r2)}};
    check("readv", readv(p[0], rv, 2), 5);
    check("readv spread", r1[0] == 'a' && r1[1] == 'b' && !strcmp(r2, "cde"), 1);
    errno = 0;
    check("writev bad count -> EINVAL", writev(p[1], wv, -1) == -1 ? errno : 0, EINVAL);
    close(p[0]);
    close(p[1]);

    /* addresses */
    struct in_addr in;
    char text[INET_ADDRSTRLEN];
    check("inet_pton", inet_pton(AF_INET, "192.168.1.20", &in), 1);
    check("inet_pton order", in.s_addr, htonl(0xc0a80114));
    check("inet_pton bad", inet_pton(AF_INET, "192.168.1", &in), 0);
    check("inet_pton leading zero", inet_pton(AF_INET, "1.02.3.4", &in), 0);
    check("inet_ntop", strcmp(inet_ntop(AF_INET, &in.s_addr, text, sizeof(text)) ? text : "", "1.02.3.4") != 0, 1);
    in.s_addr = htonl(0x7f000001);
    check("inet_ntoa", strcmp(inet_ntoa(in), "127.0.0.1"), 0);
    check("inet_aton short form", inet_aton("10.1", &in) && in.s_addr == htonl(0x0a000001), 1);
    check("inet_addr", inet_addr("1.2.3.4"), htonl(0x01020304));
    check("inet_addr bad", inet_addr("1.2.3.256"), INADDR_NONE);

    /* names: only numeric hosts (no resolver) */
    struct addrinfo *res = NULL, hints = {0};
    hints.ai_socktype = SOCK_STREAM;
    check("getaddrinfo numeric", getaddrinfo("10.0.0.1", "6000", &hints, &res), 0);
    struct sockaddr_in *sin = res ? (struct sockaddr_in *) res->ai_addr : NULL;
    check("getaddrinfo address", sin && sin->sin_addr.s_addr == htonl(0x0a000001) && sin->sin_port == htons(6000), 1);
    freeaddrinfo(res);
    check("getaddrinfo name", getaddrinfo("example.org", NULL, NULL, &res), EAI_NONAME);
    check("getaddrinfo bad service", getaddrinfo("1.2.3.4", "x11", NULL, &res), EAI_SERVICE);
    struct hostent *he = gethostbyname("10.0.0.2");
    check("gethostbyname numeric", he && he->h_length == 4 && !strcmp(he->h_name, "10.0.0.2"), 1);
    check("gethostbyname name", gethostbyname("localhost") == NULL && h_errno == HOST_NOT_FOUND, 1);

    /* socket calls on a Unix socket */
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    check("getsockname", getsockname(s, (struct sockaddr *) &ss, &len), 0);
    check("getsockname family", ss.ss_family, AF_UNIX);
    int val = -1;
    len = sizeof(val);
    check("getsockopt SO_TYPE", getsockopt(s, SOL_SOCKET, SO_TYPE, &val, &len) == 0 && val == SOCK_STREAM, 1);
    check("getsockopt SO_ERROR", getsockopt(s, SOL_SOCKET, SO_ERROR, &val, &len) == 0 && val == 0, 1);
    val = 65536;
    check("setsockopt SO_SNDBUF", setsockopt(s, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)), 0);
    errno = 0;
    check("setsockopt other level -> ENOPROTOOPT", setsockopt(s, 6, 1, &val, sizeof(val)) == -1 ? errno : 0, ENOPROTOOPT);
    errno = 0;
    len = sizeof(ss);
    int nf = open("/", O_RDONLY);
    check("getpeername not a socket -> ENOTSOCK", getpeername(nf, (struct sockaddr *) &ss, &len) == -1 ? errno : 0, ENOTSOCK);
    close(nf);
    errno = 0;
    check("shutdown unconnected -> ENOTCONN", shutdown(s, SHUT_RDWR) == -1 ? errno : 0, ENOTCONN);
    close(s);

    /* no System V shared memory: programs check and go without */
    errno = 0;
    check("shmget -> ENOSYS", shmget(IPC_PRIVATE, 4096, IPC_CREAT) == -1 ? errno : 0, ENOSYS);
    check("getpagesize", getpagesize(), 4096);
    check("getdtablesize", getdtablesize() > 20, 1);

    /* gettimeofday counts below the second */
    struct timeval t0, t1;
    gettimeofday(&t0, NULL);
    usleep(120000);
    gettimeofday(&t1, NULL);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_usec - t0.tv_usec) / 1000;
    check("gettimeofday resolution", ms >= 110 && ms < 900, 1);

    /* the interval timer, alarm() and pause() */
    signal(SIGALRM, alarm_handler);
    struct itimerval it = {{0, 30000}, {0, 30000}}, old;
    check("setitimer", setitimer(ITIMER_REAL, &it, NULL), 0);
    while (alarms < 3)
        pause();
    check("SIGALRM by setitimer", alarms, 3);
    check("getitimer", getitimer(ITIMER_REAL, &old) == 0 && old.it_interval.tv_usec == 30000, 1);
    memset(&it, 0, sizeof(it));
    setitimer(ITIMER_REAL, &it, NULL);
    errno = 0;
    check("setitimer virtual -> EINVAL", setitimer(ITIMER_VIRTUAL, &it, NULL) == -1 ? errno : 0, EINVAL);
    check("alarm none pending", alarm(5), 0);
    check("alarm pending", alarm(0), 5);
    alarms = 0;
    alarm(1);
    pause();
    check("alarm fired", alarms, 1);
    signal(SIGALRM, SIG_DFL);
}

/* pthreads: a counter two threads add to under a mutex, a condition one
 * waits on, a key with a value each */
static pthread_mutex_t pt_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pt_cond = PTHREAD_COND_INITIALIZER;
static pthread_key_t pt_key;
static volatile int pt_count, pt_ready, pt_once_runs;
static volatile pthread_t pt_self;
static void *pt_key_seen;

static void pt_once_init(void) { pt_once_runs++; }

static void *pt_thread(void *arg)
{
    pt_self = pthread_self();
    pthread_setspecific(pt_key, arg);
    pt_key_seen = pthread_getspecific(pt_key);

    for (int i = 0; i < 2000; ++i) {
        pthread_mutex_lock(&pt_lock);
        int v = pt_count;
        if (i % 100 == 0)
            usleep(100);    /* the other thread tries meanwhile */
        pt_count = v + 1;
        pthread_mutex_unlock(&pt_lock);
    }

    pthread_mutex_lock(&pt_lock);
    pt_ready = 1;
    pthread_cond_signal(&pt_cond);
    pthread_mutex_unlock(&pt_lock);
    return arg;
}

static void test_pthread(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, pt_once_init);
    pthread_once(&once, pt_once_init);
    check("pthread_once", pt_once_runs, 1);

    check("pthread_key_create", pthread_key_create(&pt_key, NULL), 0);
    pthread_setspecific(pt_key, (void *) 1);

    pthread_t t;
    check("pthread_create", pthread_create(&t, NULL, pt_thread, (void *) 2), 0);

    for (int i = 0; i < 2000; ++i) {
        pthread_mutex_lock(&pt_lock);
        int v = pt_count;
        if (i % 100 == 0)
            usleep(100);
        pt_count = v + 1;
        pthread_mutex_unlock(&pt_lock);
    }

    pthread_mutex_lock(&pt_lock);
    while (!pt_ready)
        pthread_cond_wait(&pt_cond, &pt_lock);
    pthread_mutex_unlock(&pt_lock);

    void *ret = NULL;
    check("pthread_join", pthread_join(t, &ret), 0);
    check("thread's result", (long) ret, 2);
    check("mutex kept the count", pt_count, 4000);
    check("pthread_self of the thread", pthread_equal(pt_self, t), 1);
    check("pthread_self differs", pthread_equal(pthread_self(), t), 0);
    check("key value per thread", (long) pt_key_seen * 10 + (long) pthread_getspecific(pt_key), 21);
    check("trylock held -> EBUSY", pthread_mutex_lock(&pt_lock) == 0 && pthread_mutex_trylock(&pt_lock) == EBUSY, 1);
    pthread_mutex_unlock(&pt_lock);

    struct timespec past = {0, 0};
    pthread_mutex_lock(&pt_lock);
    check("cond_timedwait past -> ETIMEDOUT", pthread_cond_timedwait(&pt_cond, &pt_lock, &past), ETIMEDOUT);
    pthread_mutex_unlock(&pt_lock);

    /* the reentrant functions of <pwd.h> and <dirent.h> */
    struct passwd pw, *pwp = NULL;
    char pwbuf[256];
    check("getpwuid_r", getpwuid_r(0, &pw, pwbuf, sizeof(pwbuf), &pwp) == 0 && pwp == &pw && !strcmp(pw.pw_name, "root"), 1);
    check("getpwuid_r small -> ERANGE", getpwuid_r(0, &pw, pwbuf, 3, &pwp), ERANGE);
    check("getpwnam_r missing", getpwnam_r("nobody-here", &pw, pwbuf, sizeof(pwbuf), &pwp) == 0 && pwp == NULL, 1);

    /* pseudo-terminals, as xterm opens them */
    struct { char *dev, *opt; } pts_args = {0, 0};
    mknod("/dev/ptmx", S_IFCHR | 0666, (5 << 8) | 2);
    mkdir("/dev/pts", 0755);
    mount("devpts", "/dev/pts", 0, &pts_args);
    int ptm = posix_openpt(O_RDWR | O_NOCTTY);
    check("posix_openpt", ptm >= 0, 1);
    check("grantpt", grantpt(ptm), 0);
    check("unlockpt", unlockpt(ptm), 0);
    char *pts = ptsname(ptm);
    check("ptsname", pts && !strncmp(pts, "/dev/pts/", 9), 1);
    char ptsbuf[4];
    check("ptsname_r small -> ERANGE", ptsname_r(ptm, ptsbuf, sizeof(ptsbuf)), ERANGE);
    int ptsfd = pts ? open(pts, O_RDWR | O_NOCTTY) : -1;
    char ptc = 0;
    check("pty slave", ptsfd >= 0 && write(ptm, "x\n", 2) == 2 && read(ptsfd, &ptc, 1) == 1 && ptc == 'x', 1);
    close(ptsfd);
    close(ptm);
    errno = 0;
    check("grantpt not a master -> EINVAL", grantpt(0) == -1 ? errno : 0, EINVAL);

    /* openpty(), as st uses it, with the window size set */
    int opm = -1, ops = -1;
    char opname[32] = "";
    struct winsize ows = {24, 80, 0, 0}, gws = {0};
    check("openpty", openpty(&opm, &ops, opname, NULL, &ows), 0);
    check("openpty name", !strncmp(opname, "/dev/pts/", 9), 1);
    check("openpty winsize", ioctl(ops, TIOCGWINSZ, &gws) == 0 && gws.ws_row == 24 && gws.ws_col == 80, 1);
    check("openpty slave", write(opm, "y\n", 2) == 2 && read(ops, &ptc, 1) == 1 && ptc == 'y', 1);
    close(ops);
    close(opm);

    /* getentropy(): arc4random() kills the process if it fails */
    unsigned char ent[32] = {0}, ent2[32] = {0};
    check("getentropy", getentropy(ent, sizeof(ent)), 0);
    getentropy(ent2, sizeof(ent2));
    check("getentropy differs", memcmp(ent, ent2, sizeof(ent)) != 0, 1);
    errno = 0;
    check("getentropy > 256 -> EIO", getentropy(ent, 257) == -1 ? errno : 0, EIO);
    check("arc4random", arc4random() != arc4random() || arc4random() != arc4random(), 1);

    /* setpgrp(): a group of its own */
    pid_t pg = fork();
    if (!pg)
        _exit(setpgrp() == 0 && getpgrp() == getpid() ? 0 : 1);
    int pgst;
    waitpid(pg, &pgst, 0);
    check("setpgrp", WEXITSTATUS(pgst), 0);

    DIR *d = opendir("/");
    struct dirent de, *dep = NULL;
    int entries = 0;
    while (readdir_r(d, &de, &dep) == 0 && dep)
        entries++;
    closedir(d);
    check("readdir_r", entries > 2, 1);
}

static int vt, vcs;

static void vt_write(const char *s)
{
    write(vt, s, strlen(s));
}

/* a row of the screen, without trailing blanks */
static const char *vt_row(int row)
{
    static char buf[81];

    lseek(vcs, row * 80, SEEK_SET);
    buf[read(vcs, buf, 80) < 80 ? 0 : 80] = '\0';

    for (int i = 79; i >= 0 && buf[i] == ' '; --i)
        buf[i] = '\0';

    return buf;
}

static void test_vt(void)
{
    mknod("/dev/tty2", S_IFCHR | 0600, (4 << 8) | 2);
    mknod("/dev/vcs2", S_IFCHR | 0600, (7 << 8) | 2);
    mknod("/dev/vcsa2", S_IFCHR | 0600, (7 << 8) | 130);

    vt = open("/dev/tty2", O_RDWR | O_NOCTTY);
    vcs = open("/dev/vcs2", O_RDONLY);
    check("vt open", vt >= 0 && vcs >= 0, 1);

    struct winsize ws = {0};
    ioctl(vt, TIOCGWINSZ, &ws);
    check("vt size", ws.ws_row * 1000 + ws.ws_col, 25080);

    /* cursor addressing, colors */
    vt_write("\033[H\033[2Jhello\n\033[3;5Hab\033[1;31mc\033[0m");
    check("vt text", strcmp(vt_row(0), "hello"), 0);
    check("vt cup", strcmp(vt_row(2), "    abc"), 0);

    /* vcsa: rows, columns, cursor column and row, then characters with
     * VGA attributes (bold red is light red, 12) */
    unsigned char vcsa[4 + 2 * 80 * 3];
    int fd = open("/dev/vcsa2", O_RDONLY);
    check("vcsa read", read(fd, vcsa, sizeof(vcsa)), sizeof(vcsa));
    close(fd);
    check("vcsa header", vcsa[0] << 24 | vcsa[1] << 16 | vcsa[2] << 8 | vcsa[3], 25 << 24 | 80 << 16 | 7 << 8 | 2);
    check("vcsa attr", vcsa[4 + 2 * (2 * 80 + 6) + 1], 0x0c);

    /* insert and erase characters, erase to the end of the line */
    vt_write("\033[5;1Habcdef\033[3D\033[1@X\033[K|");
    check("vt ich el", strcmp(vt_row(4), "abcX|"), 0);
    vt_write("\033[6;1Habcdef\033[1;3H\033[6;2H\033[2P\033[1X");
    check("vt dch ech", strcmp(vt_row(5), "a ef"), 0);

    /* DEC line drawing, UTF-8 to code page 437, REP */
    vt_write("\033[7;1H\033(0lqk\033(B caf\303\251 \342\224\200x\033[2b \342\231\245\342\214\202");
    check("vt charsets", strcmp(vt_row(6), "\xda\xc4\xbf caf\x82 \xc4xxx \x03\x7f"), 0);

    /* lines scroll within the scrolling region */
    vt_write("\033[9;1Htop\033[10;12r\033[10;1HA\033[11;1HB\033[12;1HC\n\033[r");
    check("vt region", strcmp(vt_row(9), "B") || strcmp(vt_row(10), "C") ||
                       *vt_row(11) || strcmp(vt_row(8), "top"), 0);

    /* inserting and deleting lines */
    vt_write("\033[10;1H\033[L");
    check("vt il", *vt_row(9) || strcmp(vt_row(10), "B"), 0);
    vt_write("\033[M\033[M");
    check("vt dl", strcmp(vt_row(9), "C"), 0);

    /* the last column wraps on the next character */
    vt_write("\033[15;78H12345");
    check("vt wrap", strcmp(vt_row(14) + 77, "123") || strcmp(vt_row(15), "45"), 0);

    /* the alternate screen, the main one comes back */
    vt_write("\033[?1049h\033[Halt");
    check("vt alt screen", strcmp(vt_row(0), "alt"), 0);
    vt_write("\033[?1049l");
    check("vt main screen", strcmp(vt_row(0), "hello"), 0);

    /* a cursor position report is input */
    struct termios tios;
    tcgetattr(vt, &tios);
    cfmakeraw(&tios);
    tios.c_cc[VMIN] = 0;
    tios.c_cc[VTIME] = 10;
    tcsetattr(vt, TCSANOW, &tios);

    char reply[16] = {0};
    vt_write("\033[3;5H\033[6n");
    read(vt, reply, sizeof(reply) - 1);
    check("vt dsr", strcmp(reply, "\033[3;5R"), 0);

    close(vcs);
    close(vt);
}

int main(void)
{
    struct { char *dev, *opt; } no_args = {0, 0};

    mount("devfs", "/dev", 0, &no_args);
    mknod("/dev/ttyS0", S_IFCHR | 0666, (4 << 8) | 64);
    out = fopen("/dev/ttyS0", "w");
    setvbuf(out, NULL, _IONBF, 0);
    fprintf(out, "aqtest: libc start\n");

    /* errno values as seen through libc */
    errno = 0;
    check("open missing -> ENOENT", open("/nonexistent", O_RDONLY) == -1 ? errno : 0, ENOENT);
    errno = 0;
    check("rmdir read-only fs -> EROFS", rmdir("/tmp") == -1 ? errno : 0, EROFS);
    errno = 0;
    check("close bad fd -> EBADF", close(99) == -1 ? errno : 0, EBADF);

    /* signal numbers: the kernel must agree on SIGUSR1 (30 in newlib) */
    signal(SIGUSR1, handler);
    check("kill(self, SIGUSR1)", kill(getpid(), SIGUSR1), 0);
    check("SIGUSR1 handler ran", got_signal, SIGUSR1);

    errno = 0;
    check("kill bad signal -> EINVAL", kill(getpid(), 99) == -1 ? errno : 0, EINVAL);

    /* struct stat layout and times */
    mount("tmpfs", "/tmp", 0, NULL);

    FILE *f = fopen("/tmp/file", "w");
    check("fopen for writing", f != NULL, 1);
    fputs("first line\nsecond line\n", f);
    fclose(f);

    struct stat st;
    check("stat", stat("/tmp/file", &st), 0);
    check("st_size", st.st_size, 23);
    check("S_ISREG", S_ISREG(st.st_mode), 1);
    check("st_mtime is recent", st.st_mtime > 1500000000, 1);

    /* stdio reading and getline */
    f = fopen("/tmp/file", "r");
    char *line = NULL;
    size_t cap = 0;
    check("getline", getline(&line, &cap, f), 11);
    check("getline contents", strcmp(line, "first line\n"), 0);
    free(line);
    fclose(f);

    /* directories */
    DIR *dir = opendir("/tmp");
    int found = 0;
    struct dirent *ent;
    while (dir && (ent = readdir(dir)))
        if (!strcmp(ent->d_name, "file"))
            found = 1;
    if (dir)
        closedir(dir);
    check("readdir finds the file", found, 1);

    /* pipes, fork and wait */
    int p[2];
    check("pipe", pipe(p), 0);

    pid_t pid = fork();
    if (pid == 0) {
        close(p[0]);
        write(p[1], "hello", 5);
        _exit(7);
    }

    close(p[1]);
    char buf[16] = {0};
    ssize_t n, total = 0;
    while ((n = read(p[0], buf + total, sizeof(buf) - 1 - total)) > 0)
        total += n;
    close(p[0]);

    int status;
    check("waitpid", waitpid(pid, &status, 0), pid);
    check("child exit status", WEXITSTATUS(status), 7);
    check("pipe data then EOF", total, 5);
    check("pipe contents", strcmp(buf, "hello"), 0);

    /* misc */
    struct utsname uts;
    check("uname", uname(&uts), 0);
    check("sysname", strcmp(uts.sysname, "AquilaOS"), 0);
    check("time", time(NULL) > 1500000000, 1);

    /* crypt: vectors from the SHA-512 crypt specification */
    const char *h = crypt("Hello world!", "$6$saltstring");
    check("crypt default rounds", h && !strcmp(h, "$6$saltstring$svn8UoSVapNtMuq1ukKS4tPQd8iKwSMHWjl/"
        "O817G3uBnIFNjnQJuesI68u4OTLiBFdcbYEdFCoEOfaS35inz1"), 1);
    h = crypt("the minimum number is still observed", "$6$rounds=10$roundstoolow");
    check("crypt minimum rounds", h && !strcmp(h, "$6$rounds=1000$roundstoolow$kUMsbe306n21p9R.FRkW3IGn."
        "S9NPN0x50YhH1xhLsPuWGsUSklZt58jaTfF4ZEQpyUNGc0dqbpBYYBaHHrsX."), 1);
    errno = 0;
    check("crypt unsupported -> EINVAL", crypt("x", "$1$salt") == NULL ? errno : 0, EINVAL);

    /* passwd and shadow (etc/ comes from tests/libc/rootfs) */
    struct passwd *pw = getpwnam("tester");
    check("getpwnam", pw != NULL, 1);
    check("pw_uid", pw ? (long) pw->pw_uid : -1, 1000);

    struct spwd *sp = getspnam("tester");
    check("getspnam", sp != NULL, 1);
    check("sp_lstchg", sp ? sp->sp_lstchg : -2, 19000);
    check("sp_inact empty", sp ? sp->sp_inact : -2, -1);
    check("password verifies", sp && !strcmp(crypt("tester-password", sp->sp_pwdp), sp->sp_pwdp), 1);
    check("wrong password fails", sp && strcmp(crypt("tester-passwore", sp->sp_pwdp), sp->sp_pwdp) != 0, 1);
    check("getspnam unknown", getspnam("nobody") == NULL, 1);

    /* dropping privileges */
    pid = fork();
    if (pid == 0)
        _exit(setgid(100) || setuid(1000) || getuid() != 1000 || setuid(0) == 0 ||
              getspnam("tester") != NULL);
    check("privilege drop", waitpid(pid, &status, 0) == pid && WEXITSTATUS(status) == 0, 1);

    /* truncate (tmpfs mounted above) */
    f = fopen("/tmp/trunc", "w");
    fputs("0123456789", f);
    fclose(f);
    check("truncate", truncate("/tmp/trunc", 4), 0);
    check("truncated size", stat("/tmp/trunc", &st) == 0 ? (long) st.st_size : -1, 4);
    int tfd = open("/tmp/trunc", O_RDWR);
    check("ftruncate grow", ftruncate(tfd, 8), 0);
    char tbuf[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    check("read grown file", read(tfd, tbuf, 8), 8);
    check("grown part zeroed", memcmp(tbuf, "0123\0\0\0\0", 8), 0);
    close(tfd);

    /* wait3, getppid */
    pid_t self = getpid();
    pid = fork();
    if (pid == 0)
        _exit(getppid() == self ? 3 : 4);
    check("wait3", wait3(&status, 0, NULL), pid);
    check("wait3 status", WEXITSTATUS(status), 3);

    /* killpg: the child makes its own group, then waits to be killed */
    pipe(p);
    pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        write(p[1], "x", 1);
        for (;;)
            getpid();
    }
    read(p[0], buf, 1);
    close(p[0]);
    close(p[1]);
    check("killpg", killpg(pid, SIGTERM), 0);
    check("killpg wait", waitpid(pid, &status, 0), pid);
    check("killed by SIGTERM", WIFSIGNALED(status) ? WTERMSIG(status) : 0, SIGTERM);

    /* signal mask: a blocked signal stays pending until sigsuspend */
    sigset_t block, old, none;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR2);
    sigemptyset(&none);
    signal(SIGUSR2, handler);
    got_signal = 0;
    check("sigprocmask block", sigprocmask(SIG_BLOCK, &block, &old), 0);
    kill(getpid(), SIGUSR2);
    check("blocked signal not delivered", got_signal, 0);
    errno = 0;
    check("sigsuspend -> EINTR", sigsuspend(&none) == -1 ? errno : 0, EINTR);
    check("delivered in sigsuspend", got_signal, SIGUSR2);
    sigprocmask(SIG_SETMASK, &old, NULL);

    /* byte order */
    check("htonl", htonl(0x01020304) == 0x04030201, 1);
    check("ntohs", ntohs(htons(0xabcd)), 0xabcd);

    /* files through the *at() calls and the functions built on them */
    check("mkdir", mkdir("/tmp/dir", 0755), 0);
    int dfd = open("/tmp/dir", O_RDONLY | O_DIRECTORY);
    check("open directory", dfd >= 0, 1);
    tfd = openat(dfd, "f", O_CREAT | O_WRONLY, 0644);
    check("openat", tfd >= 0, 1);
    write(tfd, "data", 4);
    close(tfd);
    check("fstatat", fstatat(dfd, "f", &st, 0) == 0 && st.st_size == 4, 1);
    check("symlink", symlink("dir/f", "/tmp/link"), 0);
    char lbuf[32] = {0};
    check("readlink", readlink("/tmp/link", lbuf, sizeof(lbuf)), 5);
    check("readlink contents", strcmp(lbuf, "dir/f"), 0);
    char *rp = realpath("/tmp/./link", NULL);
    check("realpath through a link", rp && !strcmp(rp, "/tmp/dir/f"), 1);
    free(rp);
    symlink("/tmp/dir", "/tmp/dl");
    check("realpath .. after a link", realpath("/tmp/dl/../dir/f", lbuf) && !strcmp(lbuf, "/tmp/dir/f"), 1);
    chdir("/tmp/dir");
    check("realpath relative", realpath("../dl/./f", lbuf) && !strcmp(lbuf, "/tmp/dir/f"), 1);
    chdir("/");
    errno = 0;
    check("realpath file as directory", realpath("/tmp/dir/f/x", lbuf) == NULL ? errno : 0, ENOTDIR);
    errno = 0;
    check("realpath missing", realpath("/tmp/nothing/x", lbuf) == NULL ? errno : 0, ENOENT);
    check("rename a directory", rename("/tmp/dir", "/tmp/dir2"), 0);
    check("renamed", stat("/tmp/dir2/f", &st), 0);
    DIR *dp = fdopendir(openat(AT_FDCWD, "/tmp/dir2", O_RDONLY | O_DIRECTORY));
    int entries = 0;
    struct dirent *de;
    while (dp && (de = readdir(dp)))
        entries += !strcmp(de->d_name, "f");
    check("fdopendir", entries, 1);
    if (dp)
        closedir(dp);
    errno = 0;
    check("rmdir not empty", rmdir("/tmp/dir2") == -1 ? errno : 0, ENOTEMPTY);
    check("unlinkat", unlinkat(AT_FDCWD, "/tmp/dir2/f", 0), 0);
    check("rmdir", rmdir("/tmp/dir2"), 0);
    close(dfd);
    check("chmod", chmod("/tmp/trunc", 0600) == 0 && stat("/tmp/trunc", &st) == 0 && (st.st_mode & 0777) == 0600, 1);
    struct timespec times[2] = {{1000, 0}, {2000, 0}};
    check("utimensat", utimensat(AT_FDCWD, "/tmp/trunc", times, 0), 0);
    check("mtime set", stat("/tmp/trunc", &st) == 0 && st.st_mtime == 2000, 1);

    /* /etc/group (tests/libc/rootfs) */
    struct group *gr = getgrnam("users");
    check("getgrnam", gr && gr->gr_gid == 100, 1);
    check("group members", gr && gr->gr_mem[0] && !strcmp(gr->gr_mem[1], "other") && !gr->gr_mem[2], 1);
    gr = getgrgid(0);
    check("getgrgid", gr && !strcmp(gr->gr_name, "root"), 1);
    check("getgrnam unknown", getgrnam("nobody") == NULL, 1);

    /* host name */
    char host[64];
    check("sethostname", sethostname("testhost", 8), 0);
    check("gethostname", gethostname(host, sizeof(host)) == 0 && !strcmp(host, "testhost"), 1);

    /* sleeping and clocks */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    check("usleep", usleep(100000), 0);
    check("sleep", sleep(1), 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    check("slept 1.1s", ms >= 1100 && ms < 2000, 1);

    /* terminals: the serial port is one, a pipe isn't */
    check("isatty tty", isatty(fileno(out)), 1);
    char *tty = ttyname(fileno(out));
    check("ttyname", tty && !strcmp(tty, "/dev/ttyS0"), 1);
    pipe(p);
    errno = 0;
    check("isatty pipe", isatty(p[0]) == 0 && errno == ENOTTY, 1);
    errno = 0;
    check("lseek pipe -> ESPIPE", lseek(p[0], 0, SEEK_CUR) == -1 ? errno : 0, ESPIPE);

    /* writing to a pipe without readers */
    close(p[0]);
    signal(SIGPIPE, SIG_IGN);
    errno = 0;
    check("EPIPE", write(p[1], "x", 1) == -1 ? errno : 0, EPIPE);
    signal(SIGPIPE, SIG_DFL);
    pid = fork();
    if (pid == 0) {
        write(p[1], "x", 1);
        _exit(0);
    }
    close(p[1]);
    check("SIGPIPE", waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) ? WTERMSIG(status) : 0, SIGPIPE);

    /* select and poll through libc */
    int sp2[2];
    pipe(sp2);
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sp2[0], &rfds);
    struct timeval tv = {0, 100000};
    check("select timeout", select(sp2[0] + 1, &rfds, NULL, NULL, &tv), 0);
    write(sp2[1], "s", 1);
    FD_ZERO(&rfds);
    FD_SET(sp2[0], &rfds);
    check("select readable", select(sp2[0] + 1, &rfds, NULL, NULL, NULL), 1);
    check("FD_ISSET", FD_ISSET(sp2[0], &rfds) != 0, 1);
    struct pollfd pfd = {sp2[0], POLLIN, 0};
    check("poll", poll(&pfd, 1, -1) == 1 && pfd.revents == POLLIN, 1);
    close(sp2[0]);
    close(sp2[1]);

    /* siginfo and sigsetjmp: a fault reports its address */
    struct sigaction fsa = {0};
    fsa.sa_sigaction = fault_handler;
    fsa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &fsa, NULL);
    int jumped = sigsetjmp(fault_jmp, 1);
    if (!jumped)
        *(volatile int *) 0x1234 = 1;
    check("SIGSEGV handler ran", jumped, SIGSEGV);
    check("si_addr", (long) fault_addr, 0x1234);
    signal(SIGSEGV, SIG_DFL);

    /* pselect: the signal blocked outside is let through while waiting */
    sigset_t blk, empty, cur;
    sigemptyset(&blk);
    sigaddset(&blk, SIGUSR1);
    sigemptyset(&empty);
    sigprocmask(SIG_BLOCK, &blk, NULL);
    signal(SIGUSR1, handler);
    got_signal = 0;
    pipe(sp2);
    pid = fork();
    if (pid == 0) {
        usleep(100000);
        kill(getppid(), SIGUSR1);
        _exit(0);
    }
    FD_ZERO(&rfds);
    FD_SET(sp2[0], &rfds);
    struct timespec pts = {2, 0};
    errno = 0;
    check("pselect -> EINTR", pselect(sp2[0] + 1, &rfds, NULL, NULL, &pts, &empty) == -1 ? errno : 0, EINTR);
    check("signal delivered in pselect", got_signal, SIGUSR1);
    sigprocmask(SIG_SETMASK, NULL, &cur);
    check("mask restored", sigismember(&cur, SIGUSR1), 1);
    sigprocmask(SIG_UNBLOCK, &blk, NULL);
    waitpid(pid, &status, 0);
    close(sp2[0]);
    close(sp2[1]);

    /* descriptors */
    tfd = open("/tmp/trunc", O_RDONLY);
    errno = 0;
    check("write read-only -> EBADF", write(tfd, "x", 1) == -1 ? errno : 0, EBADF);
    check("fsync", fsync(tfd), 0);
    close(tfd);
    errno = 0;
    check("fsync closed -> EBADF", fsync(tfd) == -1 ? errno : 0, EBADF);

    /* the serial port is init's controlling terminal */
    check("tcgetsid", tcgetsid(fileno(out)), getpid());
    check("tcflush", tcflush(fileno(out), TCIFLUSH), 0);

    /* wcwidth: control characters aren't printable (-1, not 0, which
     * programs take for a combining character) */
    check("wcwidth newline", wcwidth(L'\n'), -1);
    check("wcwidth NUL", wcwidth(L'\0'), 0);
    check("wcwidth letter", wcwidth(L'a'), 1);

    /* C99 printf formats */
    char fmt[32];
    snprintf(fmt, sizeof(fmt), "%zu %jd %hhu", (size_t) 42, (intmax_t) -7, (unsigned char) 300);
    check("printf %zu %jd %hhu", strcmp(fmt, "42 -7 44"), 0);

    /* flock: EWOULDBLOCK for a lock another open file holds */
    int lk = open("/tmp/flock", O_CREAT | O_RDWR, 0644);
    int lk2 = open("/tmp/flock", O_RDWR);
    check("flock", flock(lk, LOCK_EX), 0);
    errno = 0;
    check("flock held -> EWOULDBLOCK", flock(lk2, LOCK_SH | LOCK_NB) == -1 ? errno : 0, EWOULDBLOCK);
    close(lk);
    check("flock after close", flock(lk2, LOCK_SH | LOCK_NB), 0);
    close(lk2);

    /* reboot: only root */
    check("reboot enable Ctrl+Alt+Del", reboot(RB_ENABLE_CAD), 0);
    pid_t rb = fork();
    if (!rb) {
        setuid(1000);
        _exit(reboot(RB_AUTOBOOT) == -1 && errno == EPERM ? 0 : 1);
    }
    int rb_status;
    waitpid(rb, &rb_status, 0);
    check("reboot unprivileged -> EPERM", WEXITSTATUS(rb_status), 0);

    test_x_support();
    test_pthread();

    /* virtual terminals */
    test_vt();

    fprintf(out, "aqtest: done, fails=%d\n", fails);

    for (;;)
        getpid();
}
