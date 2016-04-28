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

void us()
{
    asm("sti");
    for(;;);
}

void kmain()
{
	load_ramdisk();
	proc_t *init = load_elf("/bin/init");
	spawn_init(init);
	
	for(;;);
    /*pmman.map(0x0, 0x1000, URWX);
    memcpy(0, us, kmain - us);
    printk("Hello from kernel!\n");
    switch_to_userspace();
    for(;;);*/
}
