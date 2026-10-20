#include <aqbox.h>
#include <crypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <pwd.h>
#include <shadow.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>

/* read a line from stdin without the newline */
static int read_line(const char *prompt, char *buf, size_t size, int echo)
{
    struct termios orig, noecho;

    if (!echo) {
        tcgetattr(0, &orig);
        noecho = orig;
        noecho.c_lflag &= ~(ECHO | ECHOE);
        tcsetattr(0, TCSAFLUSH, &noecho);
    }

    printf("%s", prompt);
    fflush(stdout);

    char *ret = fgets(buf, size, stdin);

    if (!echo) {
        tcsetattr(0, TCSAFLUSH, &orig);
        printf("\n");
    }

    if (!ret)
        return -1;

    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

/* compare without leaking where the strings differ through timing */
static int equal(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = la != lb;

    for (size_t i = 0; i < la && i < lb; ++i)
        diff |= a[i] ^ b[i];

    return !diff;
}

/* password hash of a user, from /etc/shadow if passwd says "x" */
static const char *password_hash(struct passwd *pw)
{
    if (strcmp(pw->pw_passwd, "x"))
        return pw->pw_passwd;

    struct spwd *sp = getspnam(pw->pw_name);
    return sp ? sp->sp_pwdp : NULL;
}

/*
 * Check the password of username, prompting for it unless the account
 * has none. Unknown users are prompted too so they can't be told apart.
 */
static struct passwd *authenticate(const char *username)
{
    struct passwd *pw = getpwnam(username);
    const char *hash = pw ? password_hash(pw) : NULL;

    /* an empty password field means no password */
    if (hash && !*hash)
        return pw;

    char pass[256];
    if (read_line("password: ", pass, sizeof(pass), 0))
        return NULL;

    /* "!..." and "*..." are locked accounts, crypt() never produces them */
    int ok = hash && hash[0] != '!' && hash[0] != '*';

    if (ok) {
        const char *c = crypt(pass, hash);
        ok = c && equal(c, hash);
    }

    memset(pass, 0, sizeof(pass));
    return ok ? pw : NULL;
}

AQBOX_APPLET(login)(int argc, char **argv)
{
    struct utsname utsname;
    uname(&utsname);

    printf("AquilaOS version %s\n", utsname.release);

    char username[256];

    if (read_line("login: ", username, sizeof(username), 1))
        return -1;

    struct passwd *passwd = authenticate(username);

    if (!passwd) {
        fprintf(stderr, "Login incorrect\n");
        return -1;
    }

    /* XXX Don't do this! */
    setenv("USER", passwd->pw_name, 1);
    setenv("LOGNAME", passwd->pw_name, 1);
    setenv("HOME", passwd->pw_dir, 1);
    setenv("PWD", passwd->pw_dir, 1);
    setenv("PATH", "/bin:/sbin:/usr/bin", 1);

    chdir(passwd->pw_dir);

    int motd = open("/etc/motd", O_RDONLY);
    if (motd > 0) {
        char buf[512]; int r;
        while ((r = read(motd, buf, sizeof(buf))) > 0)
            write(1, buf, r);
        write(1, "\n", 1);
        close(motd);
    }

    extern char **environ;
    char *args[] = {passwd->pw_shell, NULL}; /* XXX */
    int pid, status;

    if ((pid = fork())) {
        int r = waitpid(pid, &status, 0);
        printf("Shell returned status %x\n", status);

        if (WIFSIGNALED(status)) {   /* Terminated due to signal */
            switch (WTERMSIG(status)) {
                case SIGINT:    /* Ignore */
                    break;
                case SIGSEGV:
                    fprintf(stderr, "Segmentation fault\n");
                    break;
                default:
                    fprintf(stderr, "Terminated due to signal %d\n", WTERMSIG(status));
                    break;
            }
        }
    } else {
        /* drop root, group first while we still may */
        if (setgid(passwd->pw_gid) || setuid(passwd->pw_uid)) {
            perror("login: cannot set user id");
            exit(1);
        }

        int x = execve(passwd->pw_shell, args, environ);
        exit(x);
    }

    return 0;
}
