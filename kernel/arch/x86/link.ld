OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS {

	LMA = 0x100000;
	. = LMA;

	.boot : AT(LMA) {
		*boot.o(*)
	}

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
