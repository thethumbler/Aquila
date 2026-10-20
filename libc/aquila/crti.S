/*
 * Prologues of the _init and _fini functions, the .init/.fini sections
 * of crtbegin.o and other objects follow, crtn.S has the epilogues.
 */

    .section .init
    .global _init
    .type _init, @function
_init:
    push %ebp
    movl %esp, %ebp

    .section .fini
    .global _fini
    .type _fini, @function
_fini:
    push %ebp
    movl %esp, %ebp
