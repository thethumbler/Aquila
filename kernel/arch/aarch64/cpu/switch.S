/*
 * aarch64: where threads stop and go on in the kernel, and going to user
 * mode.
 */

/* The offsets of struct aarch64_ctx's fields (core/arch.h). */
#define CTX_X19     0
#define CTX_FP      80
#define CTX_SP      96

/* int aarch64_ctx_save(struct aarch64_ctx *ctx): saves the registers that
 * a call keeps, and the stack pointer. Returns 0, and returns again with 1
 * when aarch64_ctx_load() switches back to it. */
.global aarch64_ctx_save
aarch64_ctx_save:
    stp     x19, x20, [x0, #CTX_X19]
    stp     x21, x22, [x0, #CTX_X19 + 16]
    stp     x23, x24, [x0, #CTX_X19 + 32]
    stp     x25, x26, [x0, #CTX_X19 + 48]
    stp     x27, x28, [x0, #CTX_X19 + 64]
    stp     x29, x30, [x0, #CTX_FP]
    mov     x1, sp
    str     x1, [x0, #CTX_SP]
    mov     x0, #0
    ret

/* void aarch64_ctx_load(struct aarch64_ctx *ctx): goes back to where the
 * context was saved, with aarch64_ctx_save() returning 1. */
.global aarch64_ctx_load
aarch64_ctx_load:
    ldp     x19, x20, [x0, #CTX_X19]
    ldp     x21, x22, [x0, #CTX_X19 + 16]
    ldp     x23, x24, [x0, #CTX_X19 + 32]
    ldp     x25, x26, [x0, #CTX_X19 + 48]
    ldp     x27, x28, [x0, #CTX_X19 + 64]
    ldp     x29, x30, [x0, #CTX_FP]
    ldr     x1, [x0, #CTX_SP]
    mov     sp, x1
    mov     x0, #1
    ret

/* void aarch64_jump_user(pc, sp, x0, x1, x2, kstack): goes to user mode
 * (EL0, with its interrupts unmasked), leaving the kernel stack at
 * `kstack` for the next exception from it. The other registers are
 * cleared, so that nothing of the kernel's leaks to user mode. */
.global aarch64_jump_user
aarch64_jump_user:
    mov     sp, x5
    msr     elr_el1, x0
    msr     sp_el0, x1
    msr     spsr_el1, xzr
    mov     x0, x2
    mov     x1, x3
    mov     x2, x4
    mov     x3, xzr
    mov     x4, xzr
    mov     x5, xzr
    mov     x6, xzr
    mov     x7, xzr
    mov     x8, xzr
    mov     x9, xzr
    mov     x10, xzr
    mov     x11, xzr
    mov     x12, xzr
    mov     x13, xzr
    mov     x14, xzr
    mov     x15, xzr
    mov     x16, xzr
    mov     x17, xzr
    mov     x18, xzr
    mov     x19, xzr
    mov     x20, xzr
    mov     x21, xzr
    mov     x22, xzr
    mov     x23, xzr
    mov     x24, xzr
    mov     x25, xzr
    mov     x26, xzr
    mov     x27, xzr
    mov     x28, xzr
    mov     x29, xzr
    mov     x30, xzr
    eret

/* void aarch64_goto(fn, sp): runs `fn` on the stack `sp`, as if nothing
 * called it (it doesn't return). */
.global aarch64_goto
aarch64_goto:
    mov     sp, x1
    mov     x29, xzr
    mov     x30, xzr
    br      x0
