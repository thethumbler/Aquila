/*
 * Process entry point
 *
 * The kernel starts a new program at _start with its stack pointer at its
 * initial stack, which is the same on every architecture (the System V
 * ABI's, as on Linux and the BSDs; see kernel/sys/execve.c). From the
 * stack pointer up, one word each:
 *
 *      argc
 *      argv[0] ... argv[argc - 1], NULL
 *      envp[0] ... NULL
 *      the auxiliary vector: (type, value) pairs, AT_NULL last
 *
 * _start passes the stack pointer to _start_c(), which finds them all.
 */

#include <stdlib.h>

extern char **environ;

/* the program's name, for err(3) and the like (the BSDs') */
char *__progname = "";

/* the auxiliary vector, for getauxval() (auxv.c) */
unsigned long *__auxv;

extern int main(int argc, char **argv, char **envp);

extern void __libc_init_array(void);
extern void __libc_fini_array(void);

void _start_c(unsigned long *sp);

/* each passes the stack pointer, with its frame pointer cleared to mark
 * the outermost frame and the stack 16-byte aligned for the call */
#if defined(__aarch64__)
asm(".globl _start\n"
    ".type _start, %function\n"
    "_start:\n"
    "    mov x29, xzr\n"
    "    mov x30, xzr\n"
    "    mov x0, sp\n"
    "    bl _start_c\n"
    "    brk #0\n");
#elif defined(__x86_64__)
asm(".globl _start\n"
    ".type _start, @function\n"
    "_start:\n"
    "    xor %ebp, %ebp\n"
    "    mov %rsp, %rdi\n"
    "    and $-16, %rsp\n"
    "    call _start_c\n"
    "    hlt\n");
#else
asm(".globl _start\n"
    ".type _start, @function\n"
    "_start:\n"
    "    xor %ebp, %ebp\n"
    "    mov %esp, %eax\n"
    "    and $-16, %esp\n"
    "    sub $12, %esp\n"
    "    push %eax\n"
    "    call _start_c\n"
    "    hlt\n");
#endif

void _start_c(unsigned long *sp)
{
    int argc = (int) sp[0];
    char **argv = (char **) (sp + 1);
    char **envp = argv + argc + 1;

    /* the auxiliary vector is after the environment's NULL */
    char **e = envp;

    while (*e)
        ++e;

    __auxv = (unsigned long *) (e + 1);

    environ = envp;

    if (argc > 0 && argv[0]) {
        char *p = argv[0];
        __progname = p;
        for (; *p; ++p)
            if (*p == '/')
                __progname = p + 1;
    }

    /* constructors (.init, .init_array) and destructors at exit */
    __libc_init_array();
    atexit(__libc_fini_array);

    exit(main(argc, argv, envp));
}
