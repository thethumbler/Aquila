#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#define DEFAULT_SHELL "/bin/aqbox"

int main()
{
    //close(0);
    //close(1);
    //close(2);

    //open("/dev/ttyS0", O_RDONLY);
    //open("/dev/ttyS0", O_WRONLY);
    //open("/dev/ttyS0", O_WRONLY);


    char *argp[] = {DEFAULT_SHELL, "sh", NULL};
    char *envp[] = {"PWD=/", "TERM=VT100", NULL};
    execve(DEFAULT_SHELL, argp, envp);
}
