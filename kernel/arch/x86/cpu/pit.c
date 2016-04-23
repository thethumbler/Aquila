#include <core/system.h>

#include <cpu/cpu.h>
#include <cpu/io.h>

static volatile int wait;
volatile uint32_t x86_pit_freq = 0;
volatile uint32_t x86_pit_ticks = 0;
volatile uint32_t x86_pit_sub_ticks = 0;

void x86_pit_handler(regs_t *r __attribute__((unused)))
{
	--wait;
	++x86_pit_sub_ticks;

	if(x86_pit_sub_ticks == x86_pit_freq)
	{
		++x86_pit_ticks;
		x86_pit_sub_ticks = 0;
	}
}

void sleep(uint32_t ms)
{
	wait = ms * x86_pit_freq / 1000;
	while(wait > 0) asm("hlt");
}

#define X86_PIT_CMD			0x43
#define X86_PIT_CHANNEL0	0x40

struct x86_pit_cmd_register
{
	uint32_t bcd	: 1;
	uint32_t mode	: 3;
	uint32_t access	: 2;
	uint32_t channel: 2;
}__attribute__((packed));

#define PIT_MODE_SQUARE_WAVE	0x3
#define PIT_ACCESS_LOHIBYTE		0x3

void x86_pit_setup(uint32_t freq)
{
	struct x86_pit_cmd_register cmd = 
	{
		.bcd = 0,
		.mode = PIT_MODE_SQUARE_WAVE,
		.access = PIT_ACCESS_LOHIBYTE,
		.channel = 0,
	};

	x86_pit_freq = freq;
	uint32_t div = 1193180/freq;  /* PIT Oscillator operates at 1.193180 MHz */

	outb(X86_PIT_CMD, cmd);
	outb(X86_PIT_CHANNEL0, (div >> 0) & 0xFF);
	outb(X86_PIT_CHANNEL0, (div >> 8) & 0xFF);

	irq_install_handler(0, x86_pit_handler);
}