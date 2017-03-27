#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;
char *hostname = "aquila";
char *user = "user";
char *pwd = "/";

void print_prompt()
{
    pwd = getenv("PWD");
    for (;;) {
        printf("[%s@%s %s]$ ", user, hostname, pwd);
        fflush(stdout);

        char buf[1024];
        memset(buf, 0, 1024);
        fgets(buf, 1024, stdin);
        printf("%s", buf);
    }
}

int main(int argc, char **argv)
{
	for (;;) {
		print_prompt();
		char buf[50];
		memset(buf, 0, 50);
		fgets(buf, 2, stdin);
		printf("%s\n", buf);
		break;
	}

	for (;;);
}
