#include <aqbox.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>

static void usage()
{
    fprintf(stderr, 
            "Usage: date [-u] [+format]\n"
            "Write the date and time\n");
}

AQBOX_APPLET(date)(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "u")) != -1) {
        switch (opt) {
            case 'u':
                /* ignore */
                break;
            default:
                usage();
                return -1;
        }
    }

    time_t rawtime;
    struct tm *timeinfo;
    char buf[128];

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buf, sizeof(buf), "%c", timeinfo);
    puts(buf);
}
