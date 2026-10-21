/* Epilogues of the _init and _fini functions, see crti.S */

    .section .init
#ifdef __x86_64__
    popq %rbp
#else
    popl %ebp
#endif
    ret

    .section .fini
#ifdef __x86_64__
    popq %rbp
#else
    popl %ebp
#endif
    ret
