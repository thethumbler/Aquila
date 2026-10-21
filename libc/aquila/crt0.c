/*
 * Process entry point
 *
 * The kernel starts a new process at _start with argc, argv and envp on
 * the stack (in that order, the stack is 16-byte aligned), the layout
 * main() expects, see kernel/arch/i386/sys/execve.c. On x86-64 they are
 * passed in registers.
 */

#include <stdlib.h>

extern char **environ;
extern int main(int argc, char **argv, char **envp);

extern void __libc_init_array(void);
extern void __libc_fini_array(void);

void _start_c(int argc, char **argv, char **envp);

#ifdef __x86_64__
asm(".globl _start\n"
    ".type _start, @function\n"
    "_start:\n"
    "    xor %ebp, %ebp\n"      /* mark the outermost frame */
    "    mov (%rsp), %rdi\n"
    "    mov 8(%rsp), %rsi\n"
    "    mov 16(%rsp), %rdx\n"
    "    call _start_c\n"
    "    hlt\n");
#else
asm(".globl _start\n"
    ".type _start, @function\n"
    "_start:\n"
    "    xor %ebp, %ebp\n"      /* mark the outermost frame */
    "    call _start_c\n"
    "    hlt\n");
#endif

void _start_c(int argc, char **argv, char **envp)
{
    environ = envp;

    /* constructors (.init, .init_array) and destructors at exit */
    __libc_init_array();
    atexit(__libc_fini_array);

    exit(main(argc, argv, envp));
}
