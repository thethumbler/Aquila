OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
INPUT(boot/builtin.o cpu/builtin.o earlycon/builtin.o mm/builtin.o sys/builtin.o chipset/builtin.o ../../core/builtin.o
../../fs/builtin.o ../../dev/builtin.o ../../sys/builtin.o ../../mm/builtin.o ../../net/builtin.o)
OUTPUT(kernel.elf)

SECTIONS {

    LMA = 0x100000;
    . = LMA;

    .boot.text : /* AT(LMA) */ {
        *boot*.o(.text*)
        *boot*.o(.rodata*)
    }

    .boot.data : /* AT(LMA) */ {
        *boot*.o(.data)
    }

    .boot.bss (NOLOAD) : /* AT(LMA) */ {
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
    
    /DISCARD/ : {
        *(.*)
    }
    
    kernel_end = . - _VMA;
}
