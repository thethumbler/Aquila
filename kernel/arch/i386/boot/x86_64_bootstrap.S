.code32
.global x86_64_bootstrap
x86_64_bootstrap:

    /* Set up basic identity paging
     * PML4 -> 0x0FF000
     * PDPT -> 0x0FE000
     * PD   -> 0x0FD000
     */

    /* Zero out 0xFE000 -> 0x100000 */
    mov $0xFE000, %edi
    mov $0x2000, %ecx
    xor %eax, %eax
    rep stosb

    /* Setup PDPT */
    mov $0x0FD000, %edi
    mov $0x200, %ecx    /* 512 entries */
    mov $0x83, %eax
    
    .__pdpt:
        movl %eax, 0(%edi)
        add $0x4, %edi
        movl $0, 0(%edi)
        add $0x4, %edi
        add $0x200000, %eax
        dec %ecx
        jnz .__pdpt

    /* Page Directory */
    movl $0xFE000, %edi
    movl $0xFD000 | 3, 0(%edi)

    /* PML4 */
    movl $0xFF000, %edi
    movl $0xFE000 | 3, 0(%edi)

    /* Set PAE and PGE */
    movl $0xA0, %eax
    mov %eax, %cr4

    /* Point CR3 to PML4 */
    movl %edi, %cr3

    /* Set LME */
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    /* Enable paging and protection */
    mov %cr0, %eax
    or $0x80000001, %eax
    mov %eax, %cr0

    /* Set up basic 64-bit GDT */
    lgdt __x86_64_bootstrap_gdt_p

    ljmp $0x8, $__x86_64_longmode

.align 4
__x86_64_bootstrap_gdt:
    /* NULL segment */
    .quad 0x0000000000000000
    /* XR_CODE, DPL0, L segment */
    .quad 0x00AF9A000000FFFF
    /* WR_DATA, DPL0, L segment */
    .quad 0x00AF92000000FFFF

.align 4
__x86_64_bootstrap_gdt_p:
    .word . - __x86_64_bootstrap_gdt - 1
    .long __x86_64_bootstrap_gdt

.code64
__x86_64_longmode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    .extern early_init
    call early_init
    
    hlt
    jmp .

/* vim: ft=gas:
 */
