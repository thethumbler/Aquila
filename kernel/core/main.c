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
#include <ds/queue.h>

void kmain()
{
	load_ramdisk();

	devfs_init();

	inode_t *dev = vfs.find(vfs_root, "dev");
	vfs.mount(dev, dev_root);

	devman_init();

	extern void devpts_init();
	devpts_init();

	inode_t *pts = vfs.mkdir(dev_root, "pts");
	extern inode_t *devpts_root;
	vfs.mount(pts, devpts_root);

	proc_t *init = load_elf("/bin/init");
	spawn_init(init);
	
	for(;;);
}
