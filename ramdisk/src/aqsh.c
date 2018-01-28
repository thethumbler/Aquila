#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>

/*
 * Global variables 
 */

extern char **environ;
char *hostname = "aquila";
char *user = "root";
char *pwd = "/";

/*
 * Built-in Commands
 */

int cmd_echo(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        printf("%s ", argv[i]);
    }

    printf("\n");

    return 0;
}

int cmd_ls(int argc, char **args)
{
    if (argc == 1) {
        DIR *d = opendir(pwd);
        struct dirent *ent;
        while (ent = readdir(d)) {
            printf("%s\n", ent->d_name);
        }
        closedir(d);
    } else if (argc == 2) {
        DIR *d = opendir(args[1]);
        struct dirent *ent;
        while (ent = readdir(d)) {
            printf("%s\n", ent->d_name);
        }
        closedir(d);
    } else {
        for (int i = 1; i < argc; ++i) {
            printf("%s:\n", args[i]);
            DIR *d = opendir(args[i]);
            struct dirent *ent;
            while (ent = readdir(d)) {
                printf("%s\n", ent->d_name);
            }
            closedir(d);
        }
    }

    return 0;
}

int cmd_mem(int argc, char *argv[])
{
    size_t s = 0x1000;
    for (int i = 0; i < 10; ++i) {
        printf("malloc(%u) = %p\n", s, malloc(s));
    }
}

int cmd_stat(int argc, char *argv[])
{

}

int cmd_cat(int argc, char *argv[])
{
    char buf[1024];

    for (int i = 1; i < argc; ++i) {
        int fd = open(argv[i], O_RDONLY);
        int r;

        while ((r = read(fd, buf, 1024)) > 0) {
            write(1, buf, r);
        }

        close(fd);
    }
}

int cmd_float(int argc, char *argv[])
{
    float sum = 0;
    for (int i = 1; i < argc; ++i) {
        float v;
        sscanf(argv[i], "%f", &v);
        sum += v;
    }

    printf("%f\n", sum);

    return 0;
}

int cmd_hd(int argc, char *argv[])
{
    char buf[1024];

    int fd = open(argv[1], O_RDONLY);
    int r;

    r = read(fd, buf, 1024);
    write(1, buf, r);

    close(fd);
}

int cmd_mount(int argc, char *argv[])
{
    // mount -t fstype [-o options] [dev] dir
    char *type = NULL, opt = NULL, dev  = NULL, dir  = NULL;

    for (int i = 1; i < argc; ++i) {
        printf("argv[%d]= %s\n", i, argv[i]);
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 't':
                    type = argv[++i];   // TODO Sanity check
                    break;
                case 'o':
                    opt = argv[++i];   // TODO Sanity check
                    break;
                default:
                    printf("Unrecognized option -%s\n", argv[i][1]);
                    return -1;
            }
        }

        if (dev)
            dir = argv[i];
        else
            dev = argv[i];

    }

    if (!type) {
        fprintf(stderr, "Filesystem type must be supplied");
        return -1;
    }

    if (dev && !dir) {
        dir = dev;
        dev = NULL;
    }

    if (!dir) {
        fprintf(stderr, "Directory must be supplied");
        return -1;
    }

    struct {
        char *dev;
        char *opt;
    } data = {dev, opt};

    printf("mount -t %s -o %s %s %s\n", type, opt, dev, dir);

    //mount(argv[2], argv[4], 0, &data);
}

int cmd_mkdir(int argc, char *argv[])
{
    // mkdir path 
    DIR *dir = opendir("/dev");
    mkdirat(dir->fd, argv[1], 0);
    closedir(dir);
}

/***********/

void print_prompt()
{
    printf("[%s@%s %s]# ", user, hostname, pwd);
    fflush(stdout);
}

void eval()
{
    char buf[1024];
    memset(buf, 0, 1024);
    fgets(buf, 1024, stdin);

    if (strlen(buf) < 2)
        return;

    char *argv[50];
    int args_i = 0;

    char *tok = strtok(buf, " \t\n");

    while (tok) {
        argv[args_i++] = tok;
        tok = strtok(NULL, " \t\n");
    }

    argv[args_i] = NULL;

    if (!strcmp(argv[0], "echo")) {
        cmd_echo(args_i, argv);
    } else if (!strcmp(argv[0], "ls")) {
        cmd_ls(args_i, argv);
    } else if (!strcmp(argv[0], "stat")) {
        cmd_stat(args_i, argv);
    } else if (!strcmp(argv[0], "cat")) {
        cmd_cat(args_i, argv);
    } else if (!strcmp(argv[0], "mem")) {
        cmd_mem(args_i, argv);
    } else if (!strcmp(argv[0], "mount")) {
        cmd_mount(args_i, argv);
    } else if (!strcmp(argv[0], "mkdir")) {
        cmd_mkdir(args_i, argv);
    } else if (!strcmp(argv[0], "float")) {
        cmd_float(args_i, argv);
    } else if (!strcmp(argv[0], "hd")) {
        cmd_hd(args_i, argv);
    } else if (!strcmp(argv[0], "lua")) {
        int cld;
        if (cld = fork()) {
            int s, pid;
            do {
                pid = waitpid(cld, &s, 0);
            } while (pid != cld);
        } else {
            int x = execve("/mnt/lua", argv, 0);
            exit(x);
        }
    } else if (!strcmp(argv[0], "kilo")) {
        int cld;
        if (cld = fork()) {
            int s, pid;
            do {
                pid = waitpid(cld, &s, 0);
            } while (pid != cld);
        } else {
            int x = execve("/etc/kilo", argv, 0);
            exit(x);
        }
    } else if (!strcmp(argv[0], "aqbox")) {
        int cld;
        if (cld = fork()) {
            int s, pid;
            do {
                pid = waitpid(cld, &s, 0);
            } while (pid != cld);
        } else {
            int x = execve("/etc/aqbox", argv, environ);
            exit(x);
        }
    } else if (!strcmp(argv[0], "lua5")) {
        int cld;
        if (cld = fork()) {
            int s, pid;
            do {
                pid = waitpid(cld, &s, 0);
            } while (pid != cld);
        } else {
            int x = execve("/etc/lua5", argv, 0);
            exit(x);
        }
    } else {
        printf("aqsh: %s: command not found\n", argv[0]);
    }
}

void shell()
{
    pwd = getenv("PWD");
    for (;;) {
        print_prompt();
        eval();
    }
}

int main(int argc, char **argv)
{
    shell();
	for (;;);
}
