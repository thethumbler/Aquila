OUTPUT_FORMAT(elf32-i386)

SECTIONS {
	VMA = 0xC0000000;
	. += VMA;
	
	.text : AT(ADDR(.text) - VMA) ALIGN(0x1000) {
		*(.text)
		*(.rodata*)
	}
	
	.data : AT(ADDR(.data) - VMA) ALIGN(0x1000) {
		*(.data)
	}
	
	.bss : AT(ADDR(.bss) - VMA) ALIGN(0x1000) {
		*(.bss)
	}
	
	/DISCARD/ : {
		*(.*)
	}

	kernel_end = . - VMA;
}
