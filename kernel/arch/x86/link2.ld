OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS {
	PMA = 0x10000;
	. = PMA;

	.boot.text : {
		multiboot.o(.text)
		multiboot.o(.rodata*)
		*(.text)
		*(.rodata)
	}
	
	.boot.data : ALIGN(0x1000) {
		multiboot.o(.data)
		*(.data)
	}
	
	.boot.bss : ALIGN(0x1000) {
		multiboot.o(.bss)
		*(.bss)
	}

	/DISCARD/ : {
		*(.*)
	}
}
