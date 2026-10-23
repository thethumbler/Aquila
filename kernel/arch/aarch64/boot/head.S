/**********************************************************************
 *                aarch64: from the boot loader to C
 *
 * The boot loader (QEMU's -kernel) takes Linux's arm64 Image: the kernel's
 * memory image, flat, with the header below. It is loaded at its physical
 * address (text_offset past the start of RAM: 0x40080000 on the virt
 * machine), and jumped to with the MMU off and x0 the physical address of
 * the devicetree.
 *
 * The kernel runs at _VMA plus its physical address (the direct map,
 * TTBR1_EL1's half). The boot tables map the first 4 GiB of physical
 * memory with 1 GiB blocks, twice: where it is (TTBR0_EL1: this code runs
 * at its physical address until the jump) and in the direct map. The
 * first GiB is the virt machine's devices: device memory; the rest
 * normal memory. pmap_init() takes the direct map's over.
 */

#include <config.h>

/* MAIR_EL1: attribute 0 normal memory (write-back), 1 device (nGnRE) */
#define MAIR            0x04FF

/* a level 1 block of 1 GiB: normal (attribute 0, inner shareable,
 * accessed), or device (attribute 1, never executed) */
#define BLOCK_NORMAL    0x701
#define BLOCK_DEVICE    0x0060000000000405

/* TCR_EL1: 48-bit halves (T0SZ, T1SZ 16), 4 KiB granules, tables walked
 * cacheable and inner shareable; the physical address size (IPS) is the
 * CPU's, added below */
#define TCR             0xB5103510

/* SCTLR_EL1: the MMU (M), the data and instruction caches (C, I) */
#define SCTLR_M         (1 << 0)
#define SCTLR_A         (1 << 1)
#define SCTLR_C         (1 << 2)
#define SCTLR_I         (1 << 12)
#define SCTLR_WXN       (1 << 19)

.section .text.head, "ax"

/* the Image header (Linux's Documentation/arch/arm64/booting.rst) */
.global _start
_start:
    b       primary_entry       /* code0 */
    nop                         /* code1 */
    .quad   0x80000             /* text_offset: from a 2 MiB aligned base */
    .quad   __image_size        /* image_size, its .bss included */
    .quad   0x2                 /* flags: little endian, 4 KiB pages */
    .quad   0, 0, 0
    .ascii  "ARM\x64"           /* magic */
    .long   0

primary_entry:
    mov     x21, x0             /* the devicetree */

    /* EL2 (QEMU with virtualization=on): down to EL1, its timers ours */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    1f

    mov     x0, #(1 << 31)      /* HCR_EL2.RW: EL1 is aarch64 */
    msr     hcr_el2, x0
    mov     x0, #3              /* EL1 reads the counters, sets the timer */
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr
    mov     x0, #0x3c5          /* EL1h, D A I F masked */
    msr     spsr_el2, x0
    adr     x0, 1f
    msr     elr_el2, x0
    eret

1:
    msr     daifset, #0xf

    /* .bss zeroed: adrp is relative to the PC, the physical one now, so
     * symbols come out as their physical addresses */
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
2:
    cmp     x0, x1
    b.hs    3f
    str     xzr, [x0], #8
    b       2b
3:

    /* the level 1 table: 4 GiB, the first one device memory */
    adrp    x0, boot_l1
    ldr     x1, =BLOCK_DEVICE
    str     x1, [x0]
    mov     x2, #1
4:
    ldr     x1, =BLOCK_NORMAL
    orr     x1, x1, x2, lsl #30
    str     x1, [x0, x2, lsl #3]
    add     x2, x2, #1
    cmp     x2, #4
    b.ne    4b

    /* both halves' level 0 tables: their first entry is it (VA 0 and
     * the direct map's start) */
    adrp    x3, boot_l0_lo
    adrp    x4, boot_l0_hi
    orr     x1, x0, #3          /* a table */
    str     x1, [x3]
    str     x1, [x4]

    ldr     x1, =MAIR
    msr     mair_el1, x1

    ldr     x1, =TCR
    mrs     x2, id_aa64mmfr0_el1
    and     x2, x2, #7          /* PARange */
    orr     x1, x1, x2, lsl #32 /* IPS */
    msr     tcr_el1, x1

    msr     ttbr0_el1, x3
    msr     ttbr1_el1, x4
    isb
    tlbi    vmalle1
    dsb     nsh
    isb

    mrs     x1, sctlr_el1
    orr     x1, x1, #SCTLR_M
    orr     x1, x1, #SCTLR_C
    orr     x1, x1, #SCTLR_I
    bic     x1, x1, #SCTLR_A
    bic     x1, x1, #SCTLR_WXN
    msr     sctlr_el1, x1
    isb

    /* to the kernel's addresses */
    ldr     x1, =high_entry
    br      x1

high_entry:
    ldr     x1, =boot_stack_top
    mov     sp, x1
    mov     x29, xzr
    mov     x30, xzr

    mov     x0, x21
    bl      aarch64_init

5:
    wfi
    b       5b

.section .bss
.balign 4096
.global boot_l0_lo, boot_l0_hi, boot_l1
boot_l0_lo:     .skip 4096
boot_l0_hi:     .skip 4096
boot_l1:        .skip 4096

.balign 16
boot_stack:     .skip 16384
boot_stack_top:
