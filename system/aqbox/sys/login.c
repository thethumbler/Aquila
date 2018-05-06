#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <sys/aq_auth.h>

AQBOX_APPLET(login)(int argc, char **argv)
{
    struct utsname utsname;
    uname(&utsname);

    printf("AquilaOS version %s\n", utsname.release);

    char username[1024], pass[1024];
    size_t len = 0;

    printf("Username: ");
    fgets(username, 1024, stdin);
    len = strlen(username);
    if (username[len-1] == '\n')
        username[len-1] = 0;

    struct termios orig;
    tcgetattr(0, &orig);
    struct termios new = orig;
    new.c_lflag &= ~(ECHO | ECHOE);
    tcsetattr(0, TCSAFLUSH, &new);

    fflush(stdin);
    printf("Password: ");
    fgets(pass, 1024, stdin);
    len = strlen(pass);
    if (pass[len-1] == '\n')
        pass[len-1] = 0;

    tcsetattr(0, TCSAFLUSH, &orig);
    printf("\n");
    fflush(stdout);

    struct passwd *passwd = NULL;

    if (!(passwd = getpwnam(username))) {
        fprintf(stderr, "Login failed\n");
        return -1;
    }

    if (aquila_auth(passwd->pw_uid, pass)) {
        fprintf(stderr, "Login failed\n");
        return -1;
    }

    /* We are now running with PID=pw_uid */

    /* XXX Don't do this! */
    setenv("USER", username, 1);
    setenv("HOME", passwd->pw_dir, 1);
    setenv("PWD", passwd->pw_dir, 1);
    setenv("PATH", "/bin:/sbin:/usr/bin", 1);

    chdir(passwd->pw_dir);

    extern char **environ;
    char *args[] = {passwd->pw_shell, NULL}; /* XXX */
    int pid, status;
    if (pid = fork())
        waitpid(pid, &status, 0);
    else
        execve(passwd->pw_shell, args, environ);
}
