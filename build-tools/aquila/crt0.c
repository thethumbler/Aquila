#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
 
extern void _exit(int code);
extern int main();
void _init(void);
 
asm(
"\
.globl _start \n\
_start: \n\
"
#if __x86_64__
"\
    pop %rdi\n\
    pop %rsi\n\
    pop %rdx\n\
" 
#endif
"\
	call _start_c \n\
	jmp . \n\
");

void _start_c(int argc, char **argv, char **env)
{
    /* Populate environment variables */
    if (env)
        for (; *env; ++env) putenv(*env);

    //_init();
    _exit(main(argc, argv));
}
