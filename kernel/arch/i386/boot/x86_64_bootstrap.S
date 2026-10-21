/**********************************************************************
 *              x86-64: from the multiboot entry to long mode
 *
 * The kernel runs at -2 GiB (_VMA). The boot tables map the first GiB of
 * physical memory three times, with 2 MiB pages: where it is (this code
 * runs at its physical address until the jump), at the kernel's addresses
 * and in the direct map (ARCH_DMAP_BASE). They are in the kernel's .bss,
 * reached at their physical addresses here; pmap_init() takes them over.
 */

#include <config.h>

#define KERNEL_VMA  0xFFFFFFFF80000000
#define PHYS(sym)   ((sym) - KERNEL_VMA)

#define PG_P        0x01
#define PG_W        0x02
#define PG_LARGE    0x80

.code32
.global x86_64_bootstrap
x86_64_bootstrap:
    /* the page directory: the first GiB, 512 pages of 2 MiB */
    movl $PHYS(boot_pd), %edi
    movl $(PG_P | PG_W | PG_LARGE), %eax
    xorl %ecx, %ecx
1:
    movl %eax, (%edi, %ecx, 8)
    movl $0, 4(%edi, %ecx, 8)
    addl $0x200000, %eax
    incl %ecx
    cmpl $512, %ecx
    jne 1b

    /* identity (PML4 0) and the direct map (PML4 256) share a PDPT, the
     * kernel's -2 GiB are the PDPT entry 510 of the PML4 entry 511 */
    movl $(PHYS(boot_pd) + PG_P + PG_W), PHYS(boot_pdpt_low)
    movl $(PHYS(boot_pd) + PG_P + PG_W), PHYS(boot_pdpt_high) + 510 * 8
    movl $(PHYS(boot_pdpt_low) + PG_P + PG_W), PHYS(boot_pml4)
    movl $(PHYS(boot_pdpt_low) + PG_P + PG_W), PHYS(boot_pml4) + 256 * 8
    movl $(PHYS(boot_pdpt_high) + PG_P + PG_W), PHYS(boot_pml4) + 511 * 8

    /* PAE and global pages */
    movl %cr4, %eax
    orl $0xA0, %eax
    movl %eax, %cr4

    movl $PHYS(boot_pml4), %eax
    movl %eax, %cr3

    /* long mode (EFER.LME) */
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    /* paging, and write protection in the kernel too (copy-on-write) */
    movl %cr0, %eax
    orl $0x80010000, %eax
    movl %eax, %cr0

    lgdt boot_gdt_pointer
    ljmp $0x08, $long_mode

.code64
long_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* to the kernel's addresses */
    movabsq $x86_64_start, %rax
    jmp *%rax

.align 8
boot_gdt:
    .quad 0x0000000000000000    /* null */
    .quad 0x00AF9A000000FFFF    /* code, 64-bit */
    .quad 0x00CF92000000FFFF    /* data */
boot_gdt_end:

boot_gdt_pointer:
    .word boot_gdt_end - boot_gdt - 1
    .long boot_gdt

/*
 * In the kernel's addresses: its stack, then the 32-bit kernel's next step
 */
.section .text.x86_64_start, "ax"
.global x86_64_start
x86_64_start:
    movabsq $boot_stack64_top, %rsp
    xorq %rbp, %rbp

    .extern x86_cpu_init
    call x86_cpu_init

    cli
1:  hlt
    jmp 1b

.section .bss
.align 4096
.global boot_pml4
boot_pml4:      .skip 4096
boot_pdpt_low:  .skip 4096
boot_pdpt_high: .skip 4096
boot_pd:        .skip 4096

.align 16
boot_stack64:   .skip 16384
boot_stack64_top:

/* vim: ft=gas:
 */
