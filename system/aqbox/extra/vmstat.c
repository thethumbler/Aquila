#include <aqbox.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define KVMEM   "/proc/kvmem"
#define BUFSIZE 1024

char buf[BUFSIZE];

AQBOX_APPLET(vmstat)(int argc, char *argv[])
{
    int err = 0;

    FILE *file = fopen(KVMEM, "r");

    if (!file) {
        fprintf(stderr, "failed to open %s: %s\n", KVMEM, strerror(errno));
        return -1;
    }

    while ((fgets(buf, sizeof(buf), file) != NULL)) {
        fprintf(stderr, "got %s\n", buf);
        //write(2, buf, r);
    }

	return err;
}
