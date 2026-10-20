OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
OUTPUT(kernel.elf)

SECTIONS {

    LMA = 0x100000;
    . = LMA;

    .boot.text : ALIGN(0x1000) {
        */arch/i386/boot/*.o(.text*)
        */arch/i386/boot/*.o(.rodata*)
    }

    .boot.data : ALIGN(0x1000) {
        */arch/i386/boot/*.o(.data)
    }

    .boot.bss (NOLOAD) : ALIGN(0x1000) {
        */arch/i386/boot/*.o(*)
    }

    boot_end = .;

    _VMA = 0xFFFF800000000000;
    . += _VMA;

    .text : AT(ADDR(.text) - _VMA) ALIGN(0x1000) {
        *(.text*)
        *(.rodata*)
    }

    .__minit : AT(ADDR(.__minit) - _VMA) ALIGN(0x1000) {
        __minit = .;
        *(.__minit*)
        __minit_end = .;
    }
    
    .data : AT(ADDR(.data) - _VMA) ALIGN(0x1000) {
        *(.data)
    }
    
    .bss : AT(ADDR(.bss) - _VMA) ALIGN(0x1000) {
        *(.bss)
    }
    
    /DISCARD/ : {
        *(.*)
    }
    
    kernel_end = . - _VMA;
}
