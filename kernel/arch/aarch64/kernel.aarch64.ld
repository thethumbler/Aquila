OUTPUT_FORMAT(elf64-littleaarch64)
ENTRY(_start)

/*
 * The kernel is loaded at its physical address (0x40080000 on the virt
 * machine: RAM's start plus the Image's text_offset) and runs at _VMA plus
 * it, in the direct map (boot/head.S). The flat image (KERNEL_BINARY) is
 * the memory from _start to the end of .data; .bss is zeroed by head.S.
 */

SECTIONS {
    _VMA = 0xFFFF000000000000;
    . = _VMA + 0x40080000;

    kernel_start = . - _VMA;

    .text : AT(ADDR(.text) - _VMA) {
        KEEP(*(.text.head))
        *(.text*)
    }

    .rodata : AT(ADDR(.rodata) - _VMA) ALIGN(0x1000) {
        *(.rodata*)
    }

    .__minit : AT(ADDR(.__minit) - _VMA) ALIGN(0x1000) {
        __minit = .;
        KEEP(*(.__minit*))
        __minit_end = .;
    }

    .data : AT(ADDR(.data) - _VMA) ALIGN(0x1000) {
        *(.data*)
    }

    .bss : AT(ADDR(.bss) - _VMA) ALIGN(0x1000) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(0x1000);
        __bss_end = .;
    }

    kernel_end = . - _VMA;
    __image_size = kernel_end - kernel_start;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.__mfini*)
    }
}
