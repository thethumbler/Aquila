#include <aqbox.h>
#include <aq_applets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#define AQBOX_VERSION "0.0.1"

static void usage()
{
    printf("aqbox v" AQBOX_VERSION " (Swiss-Army-Knife of AquilaOS)\n\n");
    printf("Built-in commands:\n");
    for (unsigned i = 0; i < APPLETS_NR; ++i) {
        printf("%s ", applets[i].name);
    }

    printf("\n");
}

static int bsearch_helper(const void *a, const void *b)
{
    struct applet *_a = (struct applet *) a;
    struct applet *_b = (struct applet *) b;
    return strcmp(_a->name, _b->name);
}

int (*aqbox_get_applet(char *name))()
{
    /* Perform binary search on applets */
    struct applet *applet = (struct applet *) 
        bsearch(&(struct applet){.name=name}, applets, APPLETS_NR, sizeof(*applets), bsearch_helper);

    return applet? applet->f : NULL;
}

int aqbox_run(int argc, char **argv)
{
    int (*f)() = aqbox_get_applet(argv[0]);

    if (f) {
        return f(argc, argv);
    } else {
        printf("%s: applet not found\n", argv[0]);
        return -1;
    }
}

int main(int argc, char *argv[])
{
    char *applet = basename(argv[0]);

    /* We were launched with a different name */
    if (strcmp(applet, "aqbox")) {
        argv[0] = applet;
        return aqbox_run(argc, argv);
    }

    if (argc < 2) {
        usage();
        return 0;
    } else {
        return aqbox_run(argc-1, argv+1);
    }
}
