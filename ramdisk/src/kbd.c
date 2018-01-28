#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* Keyboard */
#define ESC		0x01
#define  LSHIFT	0x2A
#define _LSHIFT	0xAA
#define  RSHIFT	0x36
#define _RSHIFT 0xB6
#define  CAPS	0x3A
#define _CAPS	0xBA
#define  LALT	0x38
#define _LALT	0xB8
#define  LCTL	0x1D
#define _LCTL	0x9D
#define F1		0x3B

uint8_t kbd_us[] = 
{
	'\0', ESC, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
	'\0', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'',
	'\0', '\0', '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
	'\0', '\0', '\0', ' ',
};

uint8_t kbd_us_shift[] = 
{
	'\0', ESC, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
	'\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
	'\0', 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"',
	'\0', '\0', '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
};


void handle_keyboard(int fd, int scancode)
{
    static char shift = 0, alt = 0, ctl = 0;

    switch (scancode) {
        case  LSHIFT:
        case  RSHIFT: shift = 1; return;

        case _LSHIFT:
        case _RSHIFT: shift = 0; return;
        case    CAPS: shift = !shift; return;

        case    LALT: alt = 1; return;
        case   _LALT: alt = 0; return;

        case    LCTL: ctl = 1; return;
        case   _LCTL: ctl = 0; return;
    }

    if (scancode < 60) {
        if (shift) {
            write(fd, &(char){(ctl? 31: -1) & kbd_us_shift[scancode]}, 1);
        } else {
            write(fd, &(char){(ctl? 31: -1) & kbd_us[scancode]}, 1);
        }
    }
}

#define STDIN  1
#define STDOUT 0

int main()
{
    for (;;) {
        int scancode;
        if (read(STDIN, &scancode, sizeof(scancode)) > 0) {
            handle_keyboard(STDOUT, scancode);
        }
    }
}
