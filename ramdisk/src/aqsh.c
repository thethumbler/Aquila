#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

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

int cmd_echo(int argc, char **args)
{
    for (int i = 1; i < argc; ++i) {
        printf("%s ", args[i]);
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

    char **args[50];
    int args_i = 0;

    char *tok = strtok(buf, " \t\n");

    while (tok) {
        args[args_i++] = tok;
        tok = strtok(NULL, " \t\n");
    }

    args[args_i] = NULL;

    if (!strcmp(args[0], "echo")) {
        cmd_echo(args_i, args);
    } else if (!strcmp(args[0], "ls")) {
        cmd_ls(args_i, args);
    } else if (!strcmp(args[0], "test")) {
        int cld;
        if (cld = fork()) {
            int s, pid;
            do {
                pid = waitpid(cld, &s, 0);
            } while (pid != cld);
        } else {
            int x = execve("/bin/prog", 0, 0);
            exit(x);
        }
    } else {
        printf("aqsh: %s: command not found\n", args[0]);
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
