#include <core/system.h>
#include <cpu/cpu.h>
#include <dev/dev.h>

/* Temporary PS/2 Keyboard driver */

const char kbd_us[] = 
{
	'\0', '\0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
	'\0', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'',
	'\0', '\0', '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
	'\0', '\0', '\0', ' ',
};

uint8_t kbd_us_shift[] = 
{
	'\0', '\0', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
	'\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
	'\0', 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"',
	'\0', '\0', '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
};

#define RELASED	0x80
#define LSHIFT	0x2A
#define RSHIFT	0x36
#define CAPS	0x3A
#define LALT	0x38
#define LCTL	0x1D

char shift = 0, ctrl = 0, alt = 0;

void ps2kbd_handler(int scancode)
{
	switch(scancode)
	{
		case LSHIFT:
		case RSHIFT:
			shift = 1;
			return;
		case LSHIFT | RELASED:
		case RSHIFT | RELASED:
			shift = 0;
			return;
		case LCTL:
			ctrl = 1;
			return;
		case LCTL | RELASED:
			ctrl = 0;
			return;
	}

	extern void tty_invoke(int chr);
	
	if(scancode <= 0x58)
	{
		if(ctrl)
		{
			char chr = 0x40 ^ kbd_us_shift[scancode];
			tty_invoke(chr);
		}

		else if(shift)
			tty_invoke(kbd_us_shift[scancode]);
		else
			tty_invoke(kbd_us[scancode]);
	}
}

void ps2kbd_register()
{
	extern void i8042_register_handler(int channel, void (*fun)(int));
	i8042_register_handler(1, ps2kbd_handler);
}

int ps2kbd_probe()
{
	ps2kbd_register();

	return 0;
}

dev_t ps2kbddev = (dev_t)
{
	.probe = ps2kbd_probe,
};