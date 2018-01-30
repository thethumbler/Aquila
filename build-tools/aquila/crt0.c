#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
 
extern void _exit(int code);
extern int main();
 
asm(" \
.globl _start \n\
_start: \n\
	call _start_c \n\
	jmp . \n\
");

void _start_c(int argc, char **argv, char **env)
{
    //environ = env;
    for (; *env; ++env)  /* Populate environment variables */
        putenv(*env);

    int ex = main(argc, argv);
    _exit(ex);
}
