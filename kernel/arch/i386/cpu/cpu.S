#include <config.h>

.global x86_cpuid_check
x86_cpuid_check:
#if ARCH_BITS==32
    pushf
    pushf
    xorl $1 << 21, (%esp)
    popf
    pushf
    pop %eax
    xorl (%esp), %eax
    andl $1 << 21, %eax
    popf
    ret
#else
    /* TODO */
#endif

/*
 * ISRs -- Interrupt Service Routines
 */

.global __x86_isr_int_num, __x86_isr_err_num
__x86_isr_int_num: .long 0
__x86_isr_err_num: .long 0

.macro ISR_NOERR v
.global __x86_isr\v
__x86_isr\v:
    cli
#if ARCH_BITS==32
    movl $0,  (__x86_isr_err_num)
    movl $\v, (__x86_isr_int_num)
#else
    movl $0,  __x86_isr_err_num(%rip)
    movl $\v, __x86_isr_int_num(%rip)
#endif
    jmp isr_handler
.endm

.macro ISR_ERR v
.global __x86_isr\v
__x86_isr\v:
    cli
#if ARCH_BITS==32
    popl (__x86_isr_err_num)
    movl $\v, (__x86_isr_int_num)
    jmp isr_handler
#else
    push %rbx
    movq 8(%rsp), %rbx
    movl %ebx, __x86_isr_err_num(%rip)
    pop  %rbx
    add  $8, %rsp
    movl $\v, __x86_isr_int_num(%rip)
    jmp isr_handler
#endif
.endm

.macro push_context
#if ARCH_BITS==32
    push %eax
    push %edx
    push %ecx
    push %ebx
    push %ebp
    push %esi
    push %edi
#else
    push %rax
    push %rdx
    push %rcx
    push %rbx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
#endif
.endm
    
.macro pop_context
#if ARCH_BITS==32
    pop %edi
    pop %esi
    pop %ebp
    pop %ebx
    pop %ecx
    pop %edx
    pop %eax
#else
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rbx
    pop %rcx
    pop %rdx
    pop %rax
#endif
.endm

/* Refer to 
 * - Intel 64 and IA-32 Architectures Software Developer’s Manual
 * - Volume 3: System Programming Guide
 * - Table 6-1. Protected-Mode Exceptions and Interrupts
 */

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31
ISR_NOERR 128

.extern __x86_isr
isr_handler:
#if ARCH_BITS==32
    push_context
    push %esp
    mov $0, %ebp
    call __x86_isr
    pop %eax
    pop_context
    iret
#else
    push_context
    mov %rsp, %rdi
    call __x86_isr
    pop_context
    iretq
#endif

/*
 * IRQs -- external interrupt requists (from PIC)
 */
.macro IRQ n, i
.global __x86_irq\n
__x86_irq\n:
#if ARCH_BITS==32
    cli
    movl $\i, (__x86_isr_int_num)
    jmp irq_stub
#else
    cli
    movl $\i, __x86_isr_int_num(%rip)
    jmp irq_stub
#endif
.endm

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47


.extern __x86_irq_handler
irq_stub:
#if ARCH_BITS==32
    push_context
    push %esp
    call __x86_irq_handler
    pop %eax
    pop_context
    iret
#else
    push_context
    mov %rsp, %rdi
    call __x86_irq_handler
    pop_context
    iretq
#endif

.global x86_jump_user
x86_jump_user:  /* eax, eip, cs, eflags, esp, ss */
#if ARCH_BITS==32
    pop  %eax   /* Caller return address */
    mov  $0x20 | 0x3, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pop  %eax   /* eax for sys_fork return */
    iret
#else
    pop  %rax   /* Caller return address */
    /* set segments */
    mov  $0x20 | 0x3, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    mov  %rdi, %rax   /* rax for sys_fork return */
    /* push registers */
    pushq %r9    /* ss */
    pushq %r8    /* rsp */
    pushq %rcx   /* rflags */
    pushq %rdx   /* cs */
    pushq %rsi   /* rip */
    iretq
#endif

.global x86_read_ip
x86_read_ip:
#if ARCH_BITS==32
    mov (%esp), %eax
#else
    mov (%rsp), %rax
#endif
    ret

.global x86_goto
x86_goto:
#if ARCH_BITS==32
    pop %ebx    /* Caller return address */
    pop %ebx    /* eip */
    pop %ebp
    pop %esp
    mov $-1, %eax /* Return -1 -> Done switching */
    jmp *%ebx
#else
    pop %rbx        /* Caller return address */
    mov %rdi, %rbx  /* rip */
    mov %rsi, %rbp
    mov %rdx, %rsp
    mov $-1, %rax /* Return -1 -> Done switching */
    jmp *%rbx
#endif


.extern internal_arch_sleep
.global x86_sleep
x86_sleep:
    push_context
    call internal_arch_sleep
    pop_context
    ret

.global x86_fork_return
x86_fork_return:
#if ARCH_BITS==32
    pop_context
    iret
#else
    pop_context
    iretq
#endif

.global return_from_signal
return_from_signal:
    mov 4(%esp), %edi
    mov %edi, %esp    /* Fix stack pointer */
    pop_context
    iret

.align 8
gdt_pointer:
    .word 0
#if ARCH_BITS==32
    .long 0
#else
    .quad 0
#endif

.global x86_lgdt
x86_lgdt:
#if ARCH_BITS==32
    movw 4(%esp), %ax
    movl 8(%esp), %ebx
    movw %ax, (gdt_pointer)
    movl %ebx, (gdt_pointer + 2)
    lgdt (gdt_pointer)
    ljmp $0x8, $1f
#else
    movw %di, gdt_pointer(%rip)
    movq %rsi, (gdt_pointer + 2)(%rip)
    lgdt gdt_pointer(%rip)
#endif
1:
    movl $0x10, %eax
    movl %eax, %ds
    movl %eax, %es
    movl %eax, %fs
    movl %eax, %gs
    movl %eax, %ss
    ret

.global x86_lidt
x86_lidt:
#if ARCH_BITS==32
    movl 4(%esp), %eax
    lidt (%eax)
#else
    lidt (%rdi)
#endif
    ret

.global x86_ltr
x86_ltr:
#if ARCH_BITS==32
    movl 4(%esp), %eax
    ltr %ax
#else
    ltr %di
#endif
    ret

/* vim: ft=gas:
 */
