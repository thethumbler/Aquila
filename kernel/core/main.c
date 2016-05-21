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
	extern void install_i8042_handler();
	install_i8042_handler();
	extern void ps2kbd_register();
	ps2kbd_register();
	asm("sti");
	for(;;);

	proc_t *init = load_elf("/bin/init");
	spawn_init(init);
	
	for(;;);
}
