.global early_init_fix_stack
early_init_fix_stack:
    mov 4(%esp), %eax
    add %eax, %esp
    ret
