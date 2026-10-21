/*
 * Prologues of the _init and _fini functions, the .init/.fini sections
 * of crtbegin.o and other objects follow, crtn.S has the epilogues.
 */

    .section .init
    .global _init
    .type _init, @function
_init:
#ifdef __x86_64__
    push %rbp
    movq %rsp, %rbp
#else
    push %ebp
    movl %esp, %ebp
#endif

    .section .fini
    .global _fini
    .type _fini, @function
_fini:
#ifdef __x86_64__
    push %rbp
    movq %rsp, %rbp
#else
    push %ebp
    movl %esp, %ebp
#endif
