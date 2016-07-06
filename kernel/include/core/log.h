#ifndef _LOG_H
#define _LOG_H

#include <core/system.h>
#include <cpu/io.h>

#define COM1 0x3F8

static inline void __log_enable()
{
		outb(COM1 + 1, 0x00);	// Disable all interrupts
		outb(COM1 + 3, 0x80);	// Enable DLAB
		outb(COM1 + 0, 0x03);	// Set divisor to 3
		outb(COM1 + 1, 0x00);
		outb(COM1 + 3, 0x03);	// 8 bits, no parity, one stop bit
		outb(COM1 + 2, 0xC7);	// Enable FIFO, clear it with 14-byte threshold
		//outb(COM1 + 4, 0x0B);	// IRQs enabled
}

static inline void __log_str(char *str)
{
	while (*str) {
		while (!(inb(COM1 + 5) & 0x20));
		outb(COM1, *str++);
	}
}

#define LOG_ENABLE() __log_enable()
#define LOG(s) __log_str(s)

#endif /* !_EARLY_CONSOLE_H */