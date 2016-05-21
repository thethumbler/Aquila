#include <core/system.h>
#include <dev/dev.h>
#include <dev/console.h>

/* Control sequences */
#define NUL 0x00 /* null character */
#define SOH 0x01 /* start of heading */
#define STX 0x02 /* start of text */
#define ETX 0x03 /* end of text */
#define EOT 0x04 /* end of transmission */
#define ENQ 0x05 /* enquiry */
#define ACK 0x06 /* acknowledge */
#define BEL 0x07 /* \a bell */
#define BS  0x08 /* \b backspace */
#define HT  0x09 /* \t horizontal tab */
#define LF  0x0A /* \n linefeed */
#define VT  0x0B /* \v vertical tab */
#define FF  0x0C /* \f form feed */
#define CR  0x0D /* \r carrige return */

void tty_invoke(int chr)	/* Invoke the TTY driver, and send character */
{
	char write[2] = {0, 0};

	if(chr < 0xF)
	{
		if(chr == BS)
			write[0] = BS;
		else
			write[0] = '^', write[1] = chr ^ 0x40;
	}
	else
		write[0] = chr;

	console.write(NULL, 0, 2, write);
}

/*size_t tty_read(inode_t *dev, size_t offset, size_t size, void *buf)
{
	return size;
}

size_t tty_write(inode_t *dev, size_t offset, size_t size, void *buf)
{
	return size;
}*/

dev_t ttydev = 
{
	.name = "tty",
	.type = CHRDEV,
	/*.read = tty_read,
	.write = tty_write,*/
};
