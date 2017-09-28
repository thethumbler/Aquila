#include <core/system.h>
#include <core/string.h>
#include <console/early_console.h>
#include <cpu/io.h>

#define SERIAL 1
#define VGA 2
#define EARLY_CONSOLE SERIAL

#if VGA==SERIAL
#error huh
#endif

#if EARLY_CONSOLE==SERIAL
#define COM1 0x3F8
static void init_com1()
{
	outb(COM1 + 1, 0x00);	// Disable all interrupts
	outb(COM1 + 3, 0x80);	// Enable DLAB
	outb(COM1 + 0, 0x03);	// Set divisor to 3
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x03);	// 8 bits, no parity, one stop bit
	outb(COM1 + 2, 0xC7);	// Enable FIFO, clear it with 14-byte threshold
	outb(COM1 + 4, 0x0B);	// IRQs enabled
}

static uint8_t serial_transmit_empty()
{
	return inb(COM1 + 5) & 0x20;
}

static void serial_write(char chr)
{
	while(!serial_transmit_empty());
	outb(COM1, chr);
}

static void serial_str(char *str)
{
	while(*str) serial_write(*str++);
}
#else
char *vga_start = (char*) 0xC00B8000;
char *vga = (char*) 0xC00B8000;
#endif

void early_console_init()
{
#if EARLY_CONSOLE==SERIAL
	init_com1();
#else
	outb(0x3D4, 0x0F);
	outb(0x3D5, 0xFF);
	outb(0x3D4, 0x0E);
	outb(0x3D5, 0xFF);

	unsigned i;
	for(i = 0; i < 80*25; ++i)
		vga_start[2*i] = 0;
#endif
}

#if EARLY_CONSOLE==SERIAL
#else
static void early_console_scroll(int n)
{
	memcpy(vga_start, vga_start + 160 * n, 160 * (25 - n));
	vga = vga_start + 160 * (25 - n);

	int i;
	for (i = 0; i < 80 * n; ++i) {
		vga[2 * i + 0] = '\0';
		vga[2 * i + 1] = 0x07;
	}
}
#endif

int early_console_puts(char *s)
{
#if EARLY_CONSOLE==SERIAL
	serial_str(s);
	return 0;
#else
	char *_s = s;
	while (*s) {
		*vga = *s++;
		vga += 2;
	}

	return (int)(s - _s);
#endif
}

int early_console_putc(char c)
{
#if EARLY_CONSOLE==SERIAL
	serial_write(c);
#else
	if (c) {
		if (c == '\n') {
			vga = vga_start + (vga - vga_start) / 160 * 160 + 160;
			if(vga - vga_start >= 160 * 25)
				early_console_scroll(1);
			return 1;
		}

		*vga = c;
		vga += 2;
		return 1;
	}
#endif
	return 0;
}
