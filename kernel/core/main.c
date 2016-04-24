#include <core/printk.h>
#include <core/string.h>
#include <mm/mm.h>

#include <fs/initramfs.h>
#include <dev/dev.h>

#include <dev/ramdev.h>
#include <fs/devfs.h>

#include <boot/multiboot.h>

#include <sys/proc.h>

void switch_to_userspace()
{
    asm ("\
    mov $0x20 | 0x3, %%ax;\
    movw %%ax, %%ds;\
    movw %%ax, %%es;\
    movw %%ax, %%fs;\
    movw %%ax, %%gs;\
    pushl $0x20 | 0x3;  /* SS  selector */ \
    pushl $0x1000;      /* ESP */ \
    pushf;              /* EFLAGS */    \
    popl %%eax; orl $0x200, %%eax; pushl %%eax; /* Set interrupts in USERPACE */\
    pushl $0x18 | 0x3;  /* CS selector */\
    pushl $0x0;         /* EIP */ \
    iret;":::"eax");
}

void us()
{
    asm("sti");
    for(;;);
}

void kmain()
{
	load_ramdisk();
	load_elf("/bin/init");
	
	for(;;);
    /*pmman.map(0x0, 0x1000, URWX);
    memcpy(0, us, kmain - us);
    printk("Hello from kernel!\n");
    switch_to_userspace();
    for(;;);*/
}
