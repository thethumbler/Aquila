OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
OUTPUT(kernel.elf)

SECTIONS {
    LMA = 0x100000;
    . = LMA;

    kernel_start = .;

    /* the boot code (arch/i386/boot/), run before paging, at its
     * physical address; not other objects with "boot" in their name */
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

    _VMA = 0xC0000000;
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

    .rel.plt : AT(ADDR(.rel.plt) - _VMA) ALIGN(0x1000) {
        *(.rel.plt)
    }

    .strtab : AT(ADDR(.strtab) - _VMA) ALIGN(0x1000) {
        *(.strtab)
    }

    .shstrtab : AT(ADDR(.shstrtab) - _VMA) ALIGN(0x1000) {
        *(.shstrtab)
    }
    
    /DISCARD/ : {
        *(.*)
    }
    
    kernel_end = . - _VMA;
}
