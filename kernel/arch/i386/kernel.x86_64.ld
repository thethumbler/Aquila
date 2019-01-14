OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
INPUT(boot/builtin.o cpu/builtin.o earlycon/builtin.o mm/builtin.o sys/builtin.o platform/builtin.o ../../core/builtin.o
../../fs/builtin.o ../../dev/builtin.o ../../sys/builtin.o ../../mm/builtin.o ../../net/builtin.o)
OUTPUT(kernel.elf)

SECTIONS {

    LMA = 0x100000;
    . = LMA;

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
