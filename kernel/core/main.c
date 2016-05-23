#include <core/printk.h>
#include <core/string.h>
#include <mm/mm.h>

#include <fs/initramfs.h>
#include <dev/dev.h>

#include <dev/ramdev.h>
#include <fs/devfs.h>

#include <boot/multiboot.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <dev/console.h>

void kmain()
{
	load_ramdisk();

	devfs_init();
	devman_init();

	inode_t *tty = vfs.find(vfs_root, "/dev/tty");

	tty->fs->write(tty, 0, 13, "HEllo, World!");

	/*asm("sti");
	for(;;);*/

	proc_t *init = load_elf("/bin/init");
	spawn_init(init);
	
	for(;;);
}
