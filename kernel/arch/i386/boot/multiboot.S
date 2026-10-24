/** \file
 * x86: the Multiboot header, and the kernel's entry point.
 *
 * A Multiboot boot loader (Limine, QEMU's `-kernel`) finds the header in
 * the kernel's first 8 KiB, loads the kernel, and jumps to `_start` in
 * 32-bit protected mode with paging off. `_start` saves what the loader
 * passed, sets up a stack, and calls the C code that moves the kernel to
 * the higher half (`x86_bootstrap()`, or `x86_64_bootstrap` on x86_64).
 */

#include <config.h>
#define ASM_FILE
#include <boot/multiboot.h>

/*
 * The header's flags: page-aligned modules, the memory map, and a
 * graphics mode if the kernel is built for one.
 */

#if MULTIBOOT_GFX
#define MULTIBOOT_GFX_ENABLE    MULTIBOOT_VIDEO_MODE
#else
#define MULTIBOOT_GFX_ENABLE    0
#endif

#if ARCH_BITS==32
#define MULTIBOOT_FLAGS  MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_GFX_ENABLE
#else
/* The loaders (QEMU's, Limine's) only take 32-bit ELF files. The header's
 * addresses (the "a.out kludge") let them load a 64-bit one as it is: the
 * file is laid out as it is in memory (see kernel.x86_64.ld). */
#define MULTIBOOT_FLAGS  MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_GFX_ENABLE | \
                         MULTIBOOT_AOUT_KLUDGE
#endif
#define MULTIBOOT_CHKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_FLAGS)

/*
 * The header itself, which the boot loader looks for.
 */

#if ARCH_BITS==64
.code32
#endif

/* struct multiboot_header */
.align 8
multiboot_header:
.long MULTIBOOT_HEADER_MAGIC    /* magic */
.long MULTIBOOT_FLAGS           /* flags */
.long MULTIBOOT_CHKSUM          /* checksum */
#if ARCH_BITS==32
.long 0                         /* header_addr */
.long 0                         /* load_addr */
.long 0                         /* load_end_addr */
.long 0                         /* bss_end_addr */
.long 0                         /* entry_addr */
#else
.long multiboot_header          /* header_addr */
.long kernel_start              /* load_addr */
.long kernel_load_end           /* load_end_addr */
.long kernel_bss_end            /* bss_end_addr */
.long _start                    /* entry_addr */
#endif

/* The graphics mode wanted; the boot loader may choose another one (with
 * Limine, its configured resolution). */
.long 0                         /* mode_type */
.long 1280                      /* width */
.long 800                       /* height */
.long 32                        /* depth */

/*
 * The kernel's entry point, where the boot loader jumps: eax holds the
 * Multiboot magic number, ebx the address of the Multiboot information.
 */

.global _start
_start:
    movl %eax, (multiboot_signature)
    movl %ebx, (multiboot_info)

    /*
     * Multiboot leaves %esp undefined, so use our own boot stack. Paging
     * is not enabled yet, but the boot sections are linked at their
     * physical addresses, so the symbol can be used as it is.
     */
#if ARCH_BITS==32
    movl $boot_stack_top, %esp
#else
    /* the stack is in the kernel's .bss: use its physical address */
    movl $(boot_stack_top - 0xFFFFFFFF80000000), %esp
#endif
    and  $-16, %esp /* align the stack to 16 bytes */

    mov  $0, %ebp

    cli

#if ARCH_BITS==32
    .extern x86_bootstrap
    call x86_bootstrap
#else
    .extern x86_64_bootstrap
    call x86_64_bootstrap
#endif
    jmp .

.global multiboot_signature
.global multiboot_info

multiboot_signature: .long 0
multiboot_info: .long 0

.section .bss
.align 16
boot_stack:
    .skip 16384
boot_stack_top:

/* vim: ft=gas:
*/
