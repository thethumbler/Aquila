/*
 * Copying to and from user memory (sys/uaccess.h: copyin, copyout,
 * copyinstr)
 *
 * The instructions that touch user memory are listed in a table
 * (x86_uaccess_fixups): a page fault on one that the fault handler can't
 * resolve (the address isn't mapped, or not so) resumes at its fixup,
 * which returns -EFAULT. A fault anywhere else in the kernel is a bug.
 */

#include <config.h>

#define EFAULT  14

.text

/* size_t arch_copy_user(void *dst, const void *src, size_t n): what wasn't
 * copied (0: all of it; a fault leaves rep movsb's count of what's left) */
.global arch_copy_user
arch_copy_user:
#if ARCH_BITS==32
    push %esi
    push %edi
    mov 12(%esp), %edi
    mov 16(%esp), %esi
    mov 20(%esp), %ecx
.Lcopy:
    rep movsb
    xor %eax, %eax
    pop %edi
    pop %esi
    ret
.Lcopy_fault:
    mov %ecx, %eax
    pop %edi
    pop %esi
    ret
#else
    mov %rdx, %rcx
.Lcopy:
    rep movsb
    xor %eax, %eax
    ret
.Lcopy_fault:
    mov %rcx, %rax
    ret
#endif

/*
 * long arch_copy_user_str(char *dst, const char *src, size_t max): the
 * string's length (its NUL copied), max if there's no NUL in max bytes,
 * -EFAULT
 */
.global arch_copy_user_str
arch_copy_user_str:
#if ARCH_BITS==32
    push %esi
    push %edi
    mov 12(%esp), %edi
    mov 16(%esp), %esi
    mov 20(%esp), %edx
    xor %eax, %eax
1:
    cmp %edx, %eax
    je 2f
.Lstr:
    movb (%esi, %eax), %cl
    movb %cl, (%edi, %eax)
    test %cl, %cl
    je 2f
    inc %eax
    jmp 1b
2:
    pop %edi
    pop %esi
    ret
.Lstr_fault:
    mov $-EFAULT, %eax
    pop %edi
    pop %esi
    ret
#else
    xor %eax, %eax
1:
    cmp %rdx, %rax
    je 2f
.Lstr:
    movb (%rsi, %rax), %cl
    movb %cl, (%rdi, %rax)
    test %cl, %cl
    je 2f
    inc %rax
    jmp 1b
2:
    ret
.Lstr_fault:
    mov $-EFAULT, %rax
    ret
#endif

/* the instructions that may fault, and where they resume then */
.section .rodata
.global x86_uaccess_fixups, x86_uaccess_fixups_end
#if ARCH_BITS==32
.align 4
x86_uaccess_fixups:
    .long .Lcopy, .Lcopy_fault
    .long .Lstr,  .Lstr_fault
#else
.align 8
x86_uaccess_fixups:
    .quad .Lcopy, .Lcopy_fault
    .quad .Lstr,  .Lstr_fault
#endif
x86_uaccess_fixups_end:
