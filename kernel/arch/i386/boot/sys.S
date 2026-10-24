/*
 * i386: moves the stack to the higher half once paging is on. The caller
 * passes the kernel's virtual base, which is added to the stack pointer;
 * the frame pointer is cleared, so stack traces end here.
 */
.global early_init_fix_stack
early_init_fix_stack:
    mov 4(%esp), %eax
    add %eax, %esp
    mov $0, %ebp
    ret
