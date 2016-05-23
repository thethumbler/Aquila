#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <cpu/io.h>
#include <dev/dev.h>
#include <fs/devfs.h>

#define VGA_START	(VMA((char*)0xB8000))

/* VGA text mode console */

static char *vga = VGA_START;

static void scroll(int n)
{
	memcpy(VGA_START, VGA_START + 160 * n, 160 * (25 - n));
	vga = VGA_START + 160 * (25 - n);

	for(int i = 0; i < 80 * n; ++i)
	{
		vga[2 * i + 0] = '\0';
		vga[2 * i + 1] = 0x07;
	}
}

void set_cursor(unsigned pos)
{
	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t)(pos & 0xFF));

	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

size_t console_write(inode_t *dev __unused, size_t offset __unused, size_t size, void *buf)
{
	printk("Got write\n");
	for(size_t i = 0; i < size; ++i)
	{
		char c = ((char*)buf)[i];
		if(c)
		{
			if(c == '\n')
			{
				vga = VGA_START + (vga - VGA_START) / 160 * 160 + 160;
				if(vga - VGA_START >= 160 * 25)
					scroll(1);
			} else 
			if(c == '\b')
			{
				if(vga - VGA_START >= 2)
				{
					vga -= 2;
					*vga = 0;
				}
			} else
			{
				if(vga - VGA_START >= 160 * 25)
					scroll(1);
				*vga = c;
				vga += 2;
			}
		}
	}

	set_cursor((vga - VGA_START)/2);

	return size;
}

static int console_probe()
{
	inode_t *console = vfs.create(dev_root, "console");

	console->dev  = &condev;

	return 0;
}

dev_t condev = 
{
	.name = "condev",
	.type = CHRDEV,
	.probe = console_probe,
	.open = vfs_generic_open,
	.read = NULL,
	.write = console_write,
};