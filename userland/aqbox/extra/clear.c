#include <aqbox.h>
#include <stdio.h>

AQBOX_APPLET(clear)(int argc, char *argv[])
{
	printf("\033[H\033[2J");    /* VT100/ANSI Clear sequence */
	fflush(stdout);
	return 0;
}
