#include <core/system.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <dev/dev.h>
#include <dev/console.h>
#include <ds/ring.h>

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

#define BUF_SIZE	1024

ring_t *tty_ring = NULL;

void tty_invoke(int chr)	/* Invoke the TTY driver, and send character */
{
	char write[2] = {0, 0};

	if(chr < 0xF)
	{
		if(chr == BS)
			write[0] = BS;
		else
		{
			write[0] = '^', write[1] = chr ^ 0x40;
			ttydev.write(NULL, 0, 2, write);
		}
	}
	else
	{
		write[0] = chr;
		ttydev.write(NULL, 0, 1, write);
	}

}

static size_t tty_read(inode_t *dev __unused, size_t offset __unused, size_t size, void *buf)
{
	while(size != 0)
	{
		size_t read = ring_read(tty_ring, size, buf);
		buf += read;
		size -= read;
	}

	return size;
}

static size_t tty_write(inode_t *dev __unused, size_t offset __unused, size_t size, void *buf)
{
	console.write(NULL, 0, size, buf);
	ring_write(tty_ring, size, buf);
	return size;
}

static int tty_probe()
{
	tty_ring = new_ring(BUF_SIZE);

	inode_t *tty = vfs.create(dev_root, "tty");
	tty->dev = &ttydev;

	return 0;
}

dev_t ttydev = 
{
	.name = "tty",
	.type = CHRDEV,
	.probe = tty_probe,
	.read = tty_read,
	.write = tty_write,
};
