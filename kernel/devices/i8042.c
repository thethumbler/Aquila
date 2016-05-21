/*
 *	Intel 8042 (PS/2 Controller) Driver
 */

/* THIS DEVICE IS NOT POPULATED */

#include <core/system.h>
#include <cpu/cpu.h>
#include <cpu/io.h>


static void (*channel1_handler)(int) = NULL;
static void (*channel2_handler)(int) = NULL;


#define FIRST_IRQ	1
#define SECOND_IRQ	12

#define DATA_PORT	0x60
#define STAT_PORT	0x64
#define IN_BUF_FULL	0x02

void i8042_read_wait()
{
	while(inb(STAT_PORT) & IN_BUF_FULL);
}

void i8042_first_handler(regs_t *r __attribute__((unused)))
{
	i8042_read_wait();
	int scancode = inb(DATA_PORT);
	if(channel1_handler)
		channel1_handler(scancode);
}

void i8042_second_handler(regs_t *r __attribute__((unused)))
{
	i8042_read_wait();
	int scancode = inb(DATA_PORT);
	if(channel2_handler)
		channel2_handler(scancode);	
}

void install_i8042_handler()
{
	irq_install_handler(FIRST_IRQ, i8042_first_handler);
	irq_install_handler(SECOND_IRQ, i8042_second_handler);
}

void i8042_register_handler(int channel, void (*fun)(int))
{
	switch(channel)
	{
		case 1:
			channel1_handler = fun;
			break;
		case 2:
			channel2_handler = fun;
			break;
	}
}