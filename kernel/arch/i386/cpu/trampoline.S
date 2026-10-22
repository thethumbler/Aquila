/*
 * The trampoline: where an application processor (AP) starts, in real
 * mode, at the page X86_TRAMPOLINE (the startup IPI's vector), copied
 * there by smp.c. It goes to protected mode, and on x86_64 to long mode,
 * with the boot CPU's CR0, CR4 and EFER and the trampoline's page tables
 * (the page identity mapped, the kernel's half), and jumps to the kernel:
 * x86_ap_start(cpu) on the CPU's idle stack.
 *
 * It runs at X86_TRAMPOLINE, not where it was linked: AT() is where its
 * labels are then.
 */

#include <config.h>
#include <cpu/smp.h>

#define AT(sym)         (X86_TRAMPOLINE + ((sym) - x86_trampoline))

/* x86_trampoline_data, struct trampoline_data (smp.c) */
#define TR_CR0      0
#define TR_CR3      8
#define TR_CR4      16
#define TR_EFER     24
#define TR_ENTRY    32
#define TR_STACK    40
#define TR_CPU      48

#if ARCH_BITS==32
#define CODE32      0x08
#else
#define CODE32      0x18
#endif

.global x86_trampoline, x86_trampoline_data, x86_trampoline_end

.code16
x86_trampoline:
    cli
    cld
    xorw %ax, %ax
    movw %ax, %ds
    lgdtl AT(tr_gdtr)

    movl %cr0, %eax
    orl $1, %eax                /* protected mode */
    movl %eax, %cr0
    ljmpl $CODE32, $AT(tr_protected)

.code32
tr_protected:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movl $AT(x86_trampoline_data), %ebx

    movl TR_CR4(%ebx), %eax
    movl %eax, %cr4
    movl TR_CR3(%ebx), %eax
    movl %eax, %cr3

#if ARCH_BITS==64
    movl $0xC0000080, %ecx      /* EFER: long mode, as the boot CPU has it */
    movl TR_EFER(%ebx), %eax
    movl TR_EFER + 4(%ebx), %edx
    wrmsr
#endif

    movl TR_CR0(%ebx), %eax     /* paging */
    movl %eax, %cr0

#if ARCH_BITS==32
    movl TR_STACK(%ebx), %esp
    pushl TR_CPU(%ebx)
    pushl $0                    /* as if it was called */
    jmp *TR_ENTRY(%ebx)
#else
    ljmp $0x08, $AT(tr_long)

.code64
tr_long:
    movl $AT(x86_trampoline_data), %ebx
    movq TR_STACK(%rbx), %rsp
    movq TR_CPU(%rbx), %rdi
    pushq $0                    /* as if it was called */
    jmpq *TR_ENTRY(%rbx)
#endif

.align 8
tr_gdt:
    .quad 0
#if ARCH_BITS==32
    .quad 0x00CF9A000000FFFF    /* 0x08: code */
    .quad 0x00CF92000000FFFF    /* 0x10: data */
#else
    .quad 0x00AF9A000000FFFF    /* 0x08: 64-bit code (the kernel's) */
    .quad 0x00CF92000000FFFF    /* 0x10: data */
    .quad 0x00CF9A000000FFFF    /* 0x18: 32-bit code, on the way */
#endif
tr_gdt_end:

tr_gdtr:
    .word tr_gdt_end - tr_gdt - 1
    .long AT(tr_gdt)

.align 8
x86_trampoline_data:
    .skip 56
x86_trampoline_end:

/* vim: ft=gas:
 */
