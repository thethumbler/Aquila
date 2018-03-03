#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/utsname.h>
#include <unistd.h>

/*
 * Built-in commands
 */

int builtin_cd(int argc, char **argv)
{
    char *path = NULL;
    if (argc == 1) {
        path = "/";
    } else if (argc == 2) {
        path = argv[1];
    } else {
        fprintf(stderr, "aqsh: cd: too many arguments\n");
        return -1;
    }

    if (chdir(path)) {
        fprintf(stderr, "aqsh: cd: %s: ", path);
        perror("");
        return errno;
    }

    /* Update environment */
    char buf[512], cwd[512];
    getcwd(cwd, 512);
    snprintf(buf, 512, "PWD=%s", cwd);
    putenv(buf);
    return 0;
}

int builtin_export(int argc, char **argv)
{
    for (int i = 0; i < argc; ++i) {
        putenv(argv[i]);    /* FIXME */
    }

    return 0;
}

struct aqsh_command {
    char *name;
    int (*f)(int, char**);
} builtin_commands[] = {
    {"cd", builtin_cd},
    {"export", builtin_export},
};

#define BUILTIN_COMMANDS_NR ((sizeof(builtin_commands)/sizeof(builtin_commands[0])))

static int bsearch_helper(const void *a, const void *b)
{
    struct aqsh_command *_a = (struct aqsh_command *) a;
    struct aqsh_command *_b = (struct aqsh_command *) b;
    return strcmp(_a->name, _b->name);
}

int (*aqsh_get_command(char *name))()
{
    /* Perform binary search on commands */
    struct aqsh_command *cmd = (struct aqsh_command *) 
        bsearch(&(struct aqsh_command){.name=name}, builtin_commands, BUILTIN_COMMANDS_NR, sizeof(builtin_commands[0]), bsearch_helper);

    return cmd? cmd->f : NULL;
}

/*
 * Global variables 
 */

extern char **environ;
char *hostname = "aquila";
char *user = "root";
char *pwd = "/";

/***********/
void print_prompt()
{
    printf("[%s@%s %s]# ", user, hostname, pwd);
    fflush(stdout);
}

int run_prog(char *name, char **argv)
{
    int cld;
    if (cld = fork()) {
        int s, pid;
        do {
            pid = waitpid(cld, &s, 0);
        } while (pid != cld);
    } else {
        int x = execve(name, argv, environ);
        exit(x);
    }
}

int eval()
{
    char buf[1024];
    memset(buf, 0, 1024);
    fgets(buf, 1024, stdin);

    if (strlen(buf) < 2)
        return 0;

    char *argv[50];
    int args_i = 0;

    char *tok = strtok(buf, " \t\n");

    while (tok) {
        argv[args_i++] = tok;
        tok = strtok(NULL, " \t\n");
    }

    argv[args_i] = NULL;

    /* Absolute path? */
    if (args_i && argv[0][0] == '/') {
        int fd = open(argv[0], O_RDONLY);
        if (fd > 0) {
            close(fd);
            return run_prog(argv[0], argv);
        } else {
            fprintf(stderr, "aqsh: %s: ", argv[0]);
            perror("");
            return errno;
        }
    }

    /* Check if command is built-in */
    int (*f)() = aqsh_get_command(argv[0]);
    if (f) return f(args_i, argv);

    /* Check if command is built-in in aqbox - FIXME */
    f = aqbox_get_applet(argv[0]);
    if (f) return f(args_i, argv);

    /* Check commands from PATH */
    char *env_path = getenv("PATH");
    if (env_path) {
        char *path = strdup(env_path);
        char *comp = strtok(path, ":");
        char buf[1024];
        do {
            sprintf(buf, "%s/%s", comp, argv[0]);
            int fd = open(buf, O_RDONLY);
            if (fd > 0) {
                close(fd);
                int ret =  run_prog(buf, argv);
                free(path);
                return ret;
            }
        } while (comp = strtok(NULL, ":"));
    }

    printf("aqsh: %s: command not found\n", argv[0]);
    return -1;
}

void shell()
{
    for (;;) {
        pwd = getenv("PWD");
        print_prompt();
        eval();
    }
}

AQBOX_APPLET(sh)(int argc, char **argv)
{
    shell();
    return 0;
}
