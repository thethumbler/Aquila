/*
 * Prologues of the _init and _fini functions, the .init/.fini sections
 * of crtbegin.o and other objects follow, crtn.S has the epilogues.
 */

    .section .init
    .global _init
    .type _init, %function
_init:
#if defined(__aarch64__)
    stp x29, x30, [sp, #-16]!
    mov x29, sp
#elif defined(__x86_64__)
    push %rbp
    movq %rsp, %rbp
#else
    push %ebp
    movl %esp, %ebp
#endif

    .section .fini
    .global _fini
    .type _fini, %function
_fini:
#if defined(__aarch64__)
    stp x29, x30, [sp, #-16]!
    mov x29, sp
#elif defined(__x86_64__)
    push %rbp
    movq %rsp, %rbp
#else
    push %ebp
    movl %esp, %ebp
#endif
