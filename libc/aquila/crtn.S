/* Epilogues of the _init and _fini functions, see crti.S */

    .section .init
    popl %ebp
    ret

    .section .fini
    popl %ebp
    ret
