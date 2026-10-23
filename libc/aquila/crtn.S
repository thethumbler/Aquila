/* Epilogues of the _init and _fini functions, see crti.S */

    .section .init
#if defined(__aarch64__)
    ldp x29, x30, [sp], #16
#elif defined(__x86_64__)
    popq %rbp
#else
    popl %ebp
#endif
    ret

    .section .fini
#if defined(__aarch64__)
    ldp x29, x30, [sp], #16
#elif defined(__x86_64__)
    popq %rbp
#else
    popl %ebp
#endif
    ret
