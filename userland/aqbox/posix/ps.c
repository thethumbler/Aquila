#include <aqbox.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

void print_info(char *ent)
{
    char path[512];
    snprintf(path, 512, "/proc/%s/status", ent);

    char buf[1024];
    FILE *status;

    if (!(status = fopen(path, "r"))) {
        perror("fopen");
        return;
    }

    size_t len = fread(buf, 1, sizeof(buf) - 1, status);
    buf[len] = 0;
    fclose(status);

    char name[64];
    int pid;
    int ppid;

    sscanf(buf, 
        "name: %s\n"
        "pid: %d\n"
        "ppid: %d\n",
        name, &pid, &ppid);

    /* process 0, the kernel's: a line for each of its threads
     * ("thread <tid> <name>: ...") */
    if (pid == 0) {
        for (char *line = strstr(buf, "\nthread "); line; line = strstr(line + 1, "\nthread ")) {
            int tid;
            char tname[32];

            if (sscanf(line, "\nthread %d %31[^:]:", &tid, tname) == 2)
                printf("%5d %5d [%s]\n", pid, ppid, tname);
        }

        return;
    }

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
    while ((dirent = readdir(proc))) {
        if (dirent->d_name[0] >= '0' && dirent->d_name[0] <= '9') {
            print_info(dirent->d_name);
        }
    }

    return 0;
}
