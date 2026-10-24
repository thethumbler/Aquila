/*
 * aarch64: the exception vectors.
 *
 * VBAR_EL1's table has 16 entries of 0x80 bytes: four for each place the
 * exception came from (EL1 with SP_EL0; EL1 with SP_EL1, the kernel; EL0
 * in aarch64, user mode; EL0 in aarch32), one for each kind of exception
 * (synchronous: a system call or a fault; IRQ; FIQ; SError).
 *
 * Each entry saves the interrupted context on the kernel stack, as a
 * struct aarch64_regs (cpu/cpu.h), calls C with it, and restores it. From
 * user mode, the kernel stack is SP_EL1 as it was when user mode was
 * entered: the thread's own, at its top (aarch64_jump_user(), and every
 * eret back). IRQs are masked on entry (PSTATE.I), and stay masked in the
 * kernel.
 */

/* The size of struct aarch64_regs, and its fields' offsets. */
#define FRAME       288     /* sizeof(struct aarch64_regs) */
#define F_SP        248
#define F_PC        256
#define F_PSTATE    264
#define F_ORIG_X0   272
#define F_ESR       280

.macro save
    sub     sp, sp, #FRAME
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]
    mrs     x21, sp_el0
    mrs     x22, elr_el1
    mrs     x23, spsr_el1
    mrs     x24, esr_el1
    stp     x21, x22, [sp, #F_SP]
    stp     x23, x0, [sp, #F_PSTATE]
    str     x24, [sp, #F_ESR]
    mov     x29, xzr            /* a stack trace ends here */
.endm

.macro restore
    ldp     x21, x22, [sp, #F_SP]
    ldr     x23, [sp, #F_PSTATE]
    msr     sp_el0, x21
    msr     elr_el1, x22
    msr     spsr_el1, x23
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    add     sp, sp, #FRAME
    eret
.endm

.macro entry handler
    save
    mov     x0, sp
    bl      \handler
    restore
.endm

.macro vector label
.balign 0x80
    b       \label
.endm

.text
.balign 0x800
.global aarch64_vectors
aarch64_vectors:
    /* EL1 with SP_EL0: never used (the kernel runs on SP_EL1) */
    vector  bad
    vector  bad
    vector  bad
    vector  bad
    /* EL1 with SP_EL1: the kernel */
    vector  sync
    vector  irq
    vector  bad
    vector  bad
    /* EL0 in aarch64: user mode */
    vector  sync
    vector  irq
    vector  bad
    vector  bad
    /* EL0 in aarch32: not supported */
    vector  bad
    vector  bad
    vector  bad
    vector  bad

sync:
    entry   aarch64_sync

irq:
    entry   aarch64_irq

bad:
    save
    mov     x0, sp
    bl      aarch64_bad
1:
    wfi
    b       1b

/* A forked child's first return to user mode, from the frame that its
 * parent's fork() made, copied to the child's own stack (sys/fork.c). The
 * kernel lock is let go of first. */
.global aarch64_fork_return
aarch64_fork_return:
    bl      kernel_unlock
    restore
