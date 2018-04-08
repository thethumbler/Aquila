#include <aqbox.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

void print_info(char *ent)
{
    char path[512];
    snprintf(path, 512, "/proc/%s/status", ent);

    char buf[512];
    FILE *status;

    if (!(status = fopen(path, "r"))) {
        perror("fopen");
        return;
    }

    fread(buf, 512, 1, status);
    fclose(status);

    char name[64];
    int pid;
    int ppid;

    sscanf(buf, 
        "name: %s\n"
        "pid: %d\n"
        "ppid: %d\n",
        name, &pid, &ppid);

    printf("%5d %5d %s\n", pid, ppid, name);
}

AQBOX_APPLET(ps)(int argc, char **argv)
{
    DIR *proc;
    struct dirent *dirent;

    if (!(proc = opendir("/proc"))) {
        perror("opendir");
        return -1;
    }

    printf("  PID  PPID PATH\n");
    while (dirent = readdir(proc)) {
        if (dirent->d_name[0] > '0' && dirent->d_name[0] <= '9') {
            print_info(dirent->d_name);
        }
    }

    return 0;
}
