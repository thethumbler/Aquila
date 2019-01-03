#include <aqbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

extern char **environ;

static void env_usage(void)
{
    fprintf(stderr,
            "Usage: env [-i] [name=value]... [utility [argument...]]\n"
            "Set the environment for command invocation\n");
}

static void __clearenv(void)
{
    environ = NULL;
}

static void print_env(void)
{
    char **e = environ;
    while (e && *e) {
        printf("%s\n", *e++);
    }
}

static int run(int argc, char *argv[])
{
    /* TODO check PATH */
    fprintf(stderr, "run: ");
    for (int i = 0; i < argc; ++i)
        fprintf(stderr, "%s ", argv[i]);
    fprintf(stderr, "\n");

    return execv(argv[0], argv);
}

#define FLAG_i  1

AQBOX_APPLET(env)(int argc, char *argv[])
{
    int flags = 0, opt;

    while ((opt = getopt(argc, argv, "i")) != -1) {
        switch (opt) {
            case 'i': flags |= FLAG_i; break;
            default: usage(); exit(1);
        }
    }

#if 0
    if (flags & FLAG_i)
        __clearenv();
#endif

    if (optind >= argc) {
        /* No arguments passed */
        print_env();
        exit(0);
    }

    for (int i = optind; i < argc; ++i) {
        if (strchr(argv[i], '=')) {
            putenv(argv[i]);
        } else {
            exit(run(argc - i, &argv[i]));
        }
    }

    print_env();
    exit(0);
}
