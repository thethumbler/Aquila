OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
INPUT(boot/builtin.o cpu/builtin.o earlycon/builtin.o mm/builtin.o sys/builtin.o platform/builtin.o ../../core/builtin.o
../../fs/builtin.o ../../dev/builtin.o ../../sys/builtin.o ../../mm/builtin.o ../../net/builtin.o)
OUTPUT(kernel.elf)

SECTIONS {
    LMA = 0x100000;
    . = LMA;

    kernel_start = .;

    .boot.text : ALIGN(0x1000) {
        *boot*.o(.text*)
        *boot*.o(.rodata*)
    }

    .boot.data : ALIGN(0x1000) {
        *boot*.o(.data)
    }

    .boot.bss (NOLOAD) : ALIGN(0x1000) {
        *boot*.o(*)
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
