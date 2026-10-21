/**********************************************************************
 *                      Multiboot support
 */

#include <config.h>
#define ASM_FILE
#include <boot/multiboot.h>

/*
 *  Multiboot definitions
 */

#if MULTIBOOT_GFX
#define MULTIBOOT_GFX_ENABLE    MULTIBOOT_VIDEO_MODE
#else
#define MULTIBOOT_GFX_ENABLE    0
#endif

#if ARCH_BITS==32
#define MULTIBOOT_FLAGS  MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_GFX_ENABLE
#else
/* loaders (QEMU's, Limine's) only take 32-bit ELF files, the addresses of
 * the header load a 64-bit one as it is: the file is laid out as it is in
 * memory (kernel.x86_64.ld) */
#define MULTIBOOT_FLAGS  MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_GFX_ENABLE | \
                         MULTIBOOT_AOUT_KLUDGE
#endif
#define MULTIBOOT_CHKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_FLAGS)

/*
 *  Multiboot lookup header
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
.long 0                         /* heload_addr */
.long 0                         /* heload_end_addr */
.long 0                         /* hebss_end_addr */
.long 0                         /* heentry_addr */
#else
.long multiboot_header          /* header_addr */
.long kernel_start              /* load_addr */
.long kernel_load_end           /* load_end_addr */
.long kernel_bss_end            /* bss_end_addr */
.long _start                    /* entry_addr */
#endif

/* the screen, which the bootloader may change (Limine's resolution:) */
.long 0                         /* mode_type */
.long 1280                      /* mowidth */
.long 800                       /* moheight */
.long 32                        /* modepth */

/*
 * Entry point of the bootloader
 */

.global _start
_start:
    movl %eax, (multiboot_signature)
    movl %ebx, (multiboot_info)

    /*
     * Multiboot leaves %esp undefined, so use our own boot stack. Paging is
     * not enabled yet, but the boot sections are linked at their physical
     * addresses so the symbol can be used as is.
     */
#if ARCH_BITS==32
    movl $boot_stack_top, %esp
#else
    /* in the kernel's .bss, at its physical address */
    movl $(boot_stack_top - 0xFFFFFFFF80000000), %esp
#endif
    and  $-16, %esp /* Align stack to 16 byte */

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
