/** \file
 * x86: the kernel's entries from interrupts and exceptions, and the
 * low-level pieces of thread switching.
 *
 * Every vector has a small stub that pushes the same frame, a
 * `struct x86_regs`, and calls the C handler: `__x86_isr()` for
 * exceptions and system calls, `__x86_irq_handler()` for device
 * interrupts and IPIs. The way back pops the frame and returns with
 * `iret`. See docs/kernel/interrupts.md.
 */

#include <config.h>

/*
 * ISRs (interrupt service routines), for the exceptions and int $0x80.
 *
 * Each stub pushes an error code (0 if the CPU didn't push one) and the
 * vector number, which are struct x86_regs' err and vec, and jumps to
 * the common code.
 */

.macro ISR_NOERR v
.global __x86_isr\v
__x86_isr\v:
    cli
    push $0
    push $\v
    jmp isr_handler
.endm

.macro ISR_ERR v
.global __x86_isr\v
__x86_isr\v:
    cli
    push $\v
    jmp isr_handler
.endm

/*
 * x86_64: while in the kernel, the GS base is the kernel's (curcpu()
 * reads it); in user mode it is user mode's. swapgs exchanges them, when
 * an interrupt came from user mode and when going back to it. The macro's
 * `cs` argument is where the interrupted CS is on the stack.
 */
.macro swapgs_if_user cs
#if ARCH_BITS==64
    testb $3, \cs(%rsp)
    jz 1f
    swapgs
1:
#endif
.endm

.macro push_context
#if ARCH_BITS==32
    push %ds
    push %es
    push %fs
    push %gs
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
    pop %gs
    pop %fs
    pop %es
    pop %ds
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

/*
 * Prepares the kernel's environment on entry from an interrupt or an
 * exception. User code may have left any data segment selectors loaded,
 * or set the direction flag, so this loads the kernel's data segments
 * (and on i386 %gs, the CPU's own segment) and clears DF. push_context
 * saves the interrupted context's selectors, and pop_context restores
 * them.
 */
.macro kernel_entry
    cld
#if ARCH_BITS==32
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov $0x38, %ax      /* X86_SEL_CPU: curcpu() */
    mov %ax, %gs
#endif
.endm

/* Which exceptions push an error code: see the Intel 64 and IA-32
 * Architectures Software Developer's Manual, Volume 3 (System Programming
 * Guide), Table 6-1, "Protected-Mode Exceptions and Interrupts". */

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
    kernel_entry
    push %esp
    mov $0, %ebp
    call __x86_isr
    pop %eax
    pop_context
    add $8, %esp        /* drop vec and err */
    iret
#else
    swapgs_if_user 24
    push_context
    kernel_entry
    mov %rsp, %rdi
    call __x86_isr
    pop_context
    swapgs_if_user 24
    add $16, %rsp       /* drop vec and err */
    iretq
#endif

/*
 * IRQs: external interrupts, from the 8259 or the APIC. Each stub pushes
 * a 0 error code and its vector; irq.c handles them.
 */
.macro IRQ n, i
.global __x86_irq\n
__x86_irq\n:
    cli
    push $0
    push $\i
    jmp irq_stub
.endm

/* IRQs 0 to 47 are vectors 32 to 79: the I/O APIC's pins, or the ISA
 * bus's 16 under the 8259. */
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
IRQ 16, 48
IRQ 17, 49
IRQ 18, 50
IRQ 19, 51
IRQ 20, 52
IRQ 21, 53
IRQ 22, 54
IRQ 23, 55
IRQ 24, 56
IRQ 25, 57
IRQ 26, 58
IRQ 27, 59
IRQ 28, 60
IRQ 29, 61
IRQ 30, 62
IRQ 31, 63
IRQ 32, 64
IRQ 33, 65
IRQ 34, 66
IRQ 35, 67
IRQ 36, 68
IRQ 37, 69
IRQ 38, 70
IRQ 39, 71
IRQ 40, 72
IRQ 41, 73
IRQ 42, 74
IRQ 43, 75
IRQ 44, 76
IRQ 45, 77
IRQ 46, 78
IRQ 47, 79

/* the local APIC's timer, and the TLB and wake IPIs */
IRQ 240, 240
IRQ 241, 241
IRQ 242, 242

/* The local APIC's spurious interrupt: there is nothing to do, and it
 * takes no EOI. */
.global __x86_spurious
__x86_spurious:
#if ARCH_BITS==32
    iret
#else
    iretq
#endif


.extern __x86_irq_handler
irq_stub:
#if ARCH_BITS==32
    push_context
    kernel_entry
    push %esp
    call __x86_irq_handler
    pop %eax
    pop_context
    add $8, %esp        /* drop vec and err */
    iret
#else
    swapgs_if_user 24
    push_context
    kernel_entry
    mov %rsp, %rdi
    call __x86_irq_handler
    pop_context
    swapgs_if_user 24
    add $16, %rsp       /* drop vec and err */
    iretq
#endif

/*
 * Starts running in user mode, for a new thread: the arguments are the
 * value for eax (rax), then eip, cs, eflags, esp and ss, as iret takes
 * them. It doesn't return.
 */
.global x86_jump_user
x86_jump_user:
#if ARCH_BITS==32
    pop  %eax   /* the caller's return address, not needed */
    mov  $0x20 | 0x3, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pop  %eax   /* eax: a new thread's result, 0 for a fork() child */
    iret
#else
    pop  %rax   /* the caller's return address, not needed */
    /* switch to user mode's GS base (keeping the kernel's) before loading
     * %gs sets it */
    swapgs
    /* the user data segments */
    mov  $0x20 | 0x3, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    mov  %rdi, %rax   /* rax: a new thread's result, 0 for a fork() child */
    /* the iretq frame */
    pushq %r9    /* ss */
    pushq %r8    /* rsp */
    pushq %rcx   /* rflags */
    pushq %rdx   /* cs */
    pushq %rsi   /* rip */
    iretq
#endif

/*
 * Jumps to an address with a given frame pointer and stack: to where a
 * thread was saved (SWITCH_OUT below), or to a function on another stack
 * (the idle loop, on the CPU's idle stack). The arguments are ip, bp and
 * sp. It doesn't return.
 */
.global x86_goto
x86_goto:
#if ARCH_BITS==32
    pop %ebx    /* the caller's return address, not needed */
    pop %ebx    /* ip */
    pop %ebp
    pop %esp
    mov $-1, %eax
    jmp *%ebx
#else
    pop %rbx        /* the caller's return address, not needed */
    mov %rdi, %rbx  /* ip */
    mov %rsi, %rbp
    mov %rdx, %rsp
    mov $-1, %rax
    jmp *%rbx
#endif


/*
 * Switches away from the current thread, to kernel_idle (it sleeps) or to
 * schedule (another thread may run). The thread's registers are pushed,
 * and where it resumes is recorded (x86_thread_saved): the label 1 below,
 * with the stack as it is. When the thread runs again, x86_goto() jumps
 * back to that label, the registers are popped, and the macro's function
 * returns to its caller.
 */
.macro SWITCH_OUT next
#if ARCH_BITS==32
    push_context
    mov %esp, %ebp
    push %ebp           /* bp */
    push %ebp           /* sp */
    push $1f            /* ip */
    call x86_thread_saved
    add $12, %esp
    call \next
1:
    mov %ebp, %esp
    pop_context
    ret
#else
    push_context
    mov %rsp, %rbp
    lea 1f(%rip), %rdi  /* ip */
    mov %rsp, %rsi      /* sp */
    mov %rbp, %rdx      /* bp */
    call x86_thread_saved
    call \next
1:
    mov %rbp, %rsp
    pop_context
    ret
#endif
.endm

.extern x86_thread_saved, kernel_idle, schedule

/* The current thread sleeps: the next ready one runs, or the CPU idles. */
.global x86_sleep
x86_sleep:
    SWITCH_OUT kernel_idle

/* The current thread goes back to the ready list, and the next one runs. */
.global x86_preempt
x86_preempt:
    SWITCH_OUT schedule

.extern kernel_unlock
/*
 * Where a fork() child starts: it lets go of the kernel lock and returns
 * to user mode through the system call's frame, which the fork copied
 * onto its stack.
 */
.global x86_fork_return
x86_fork_return:
    call kernel_unlock
#if ARCH_BITS==32
    pop_context
    add $8, %esp        /* drop vec and err */
    iret
#else
    pop_context
    swapgs
    add $16, %rsp       /* drop vec and err */
    iretq
#endif

.align 8
gdt_pointer:
    .word 0
#if ARCH_BITS==32
    .long 0
#else
    .quad 0
#endif

/* Loads a GDT (its limit and base), and reloads the segment registers
 * with the kernel's selectors. */
.global x86_lgdt
x86_lgdt:
#if ARCH_BITS==32
    movw 4(%esp), %ax
    movl 8(%esp), %ecx
    movw %ax, (gdt_pointer)
    movl %ecx, (gdt_pointer + 2)
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

/* Loads the IDT from a pointer to its descriptor (limit and base). */
.global x86_lidt
x86_lidt:
#if ARCH_BITS==32
    movl 4(%esp), %eax
    lidt (%eax)
#else
    lidt (%rdi)
#endif
    ret

/* Loads the task register with a TSS selector. */
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
