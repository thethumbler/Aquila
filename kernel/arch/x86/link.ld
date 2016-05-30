OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
INPUT(boot/boot.o cpu/cpu.o console/console.o mem/mem.o sys/sys.o ../../core/core.o
../../devices/devices.o ../../fs/fs.o ../../sys/sys.o)
OUTPUT(kernel.elf)

SECTIONS {

	LMA = 0x100000;
	. = LMA;

	.boot : AT(LMA) {
		*boot.o(*)
	}

	boot_end = .;

	_VMA = 0xC0000000;
	. += _VMA;

	.text : AT(ADDR(.text) - _VMA) ALIGN(0x1000) {
		*(.text)
		*(.rodata*)
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
