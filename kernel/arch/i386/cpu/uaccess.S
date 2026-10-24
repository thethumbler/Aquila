/** \file
 * x86: copying to and from user memory, under copyin(), copyout() and
 * copyinstr() (sys/uaccess.h).
 *
 * The instructions that touch user memory are listed in a table,
 * x86_uaccess_fixups. If one of them faults and the page fault handler
 * can't resolve the fault (the address isn't mapped, or not with that
 * permission), it resumes at its fixup, which makes the routine report
 * the failure. A fault anywhere else in the kernel is a bug.
 */

#include <config.h>

#define EFAULT  14

.text

/*
 * size_t arch_copy_user(void *dst, const void *src, size_t n)
 *
 * Copies n bytes, and returns how many weren't copied: 0 if all were, or
 * after a fault, the count of what rep movsb had left.
 */
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
 * long arch_copy_user_str(char *dst, const char *src, size_t max)
 *
 * Copies a string, with its NUL, and returns its length. Returns max if
 * there's no NUL in the first max bytes, or -EFAULT if src is a bad
 * address.
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

/* the instructions that may fault, and the fixup where each resumes */
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
