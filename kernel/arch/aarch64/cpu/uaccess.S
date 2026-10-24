/*
 * Copying to and from user memory (sys/uaccess.h: copyin(), copyout(),
 * copyinstr()), as x86's cpu/uaccess.S does.
 *
 * The instructions that touch user memory are listed in a table
 * (aarch64_uaccess_fixups: each instruction, and where to go on from). A
 * page fault on one of them that the fault handler can't resolve resumes
 * at its fixup, which returns how much wasn't copied, or -EFAULT. A fault
 * anywhere else in the kernel is a bug.
 */

#define EFAULT  14

.text

/* size_t arch_copy_user(void *dst, const void *src, size_t n): copies n
 * bytes, and returns how many weren't copied (0 if they all were). */
.global arch_copy_user
arch_copy_user:
    cbz     x2, 2f
1:
.Lcopy_load:
    ldrb    w3, [x1], #1
.Lcopy_store:
    strb    w3, [x0], #1
    subs    x2, x2, #1
    b.ne    1b
2:
    mov     x0, #0
    ret
.Lcopy_fault:
    mov     x0, x2
    ret

/*
 * long arch_copy_user_str(char *dst, const char *src, size_t max): copies
 * a string with its NUL, and returns its length, max if there's no NUL in
 * its first max bytes, or -EFAULT.
 */
.global arch_copy_user_str
arch_copy_user_str:
    mov     x3, #0
1:
    cmp     x3, x2
    b.eq    2f
.Lstr_load:
    ldrb    w4, [x1, x3]
    strb    w4, [x0, x3]
    cbz     w4, 2f
    add     x3, x3, #1
    b       1b
2:
    mov     x0, x3
    ret
.Lstr_fault:
    mov     x0, #-EFAULT
    ret

.section .rodata
.balign 8
.global aarch64_uaccess_fixups, aarch64_uaccess_fixups_end
aarch64_uaccess_fixups:
    .quad   .Lcopy_load, .Lcopy_fault
    .quad   .Lcopy_store, .Lcopy_fault
    .quad   .Lstr_load, .Lstr_fault
aarch64_uaccess_fixups_end:
