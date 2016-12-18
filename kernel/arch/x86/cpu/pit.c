/**********************************************************************
 *				Programmable Interval Timer (PIT)
 *
 *
 *	This file is part of Aquila OS and is released under the terms of
 *	GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */


#include <core/system.h>

#include <cpu/cpu.h>
#include <cpu/io.h>

static volatile int wait;
volatile uint32_t pit_freq = 0;
volatile uint32_t pit_ticks = 0;
volatile uint32_t pit_sub_ticks = 0;

static void pit_handler(regs_t *r __unused)
{
	--wait;
	++pit_sub_ticks;

	if (pit_sub_ticks == pit_freq) {
		++pit_ticks;
		pit_sub_ticks = 0;
	}
}

void sleep(uint32_t ms)
{
	wait = ms * pit_freq / 1000;
	while (wait > 0) asm("hlt");
}

#define PIT_CMD			0x43
#define PIT_CHANNEL0	0x40

struct pit_cmd_register {
	uint32_t bcd	: 1;
	uint32_t mode	: 3;
	uint32_t access	: 2;
	uint32_t channel: 2;
} __packed;

#define PIT_MODE_SQUARE_WAVE	0x3
#define PIT_ACCESS_LOHIBYTE		0x3

void pit_setup(uint32_t freq)
{
	struct pit_cmd_register cmd = {
		.bcd = 0,
		.mode = PIT_MODE_SQUARE_WAVE,
		.access = PIT_ACCESS_LOHIBYTE,
		.channel = 0,
	};

	pit_freq = freq;
	uint32_t div = 1193180/freq;  /* PIT Oscillator operates at 1.193180 MHz */

	outb(PIT_CMD, cmd);
	outb(PIT_CHANNEL0, (div >> 0) & 0xFF);
	outb(PIT_CHANNEL0, (div >> 8) & 0xFF);

	irq_install_handler(0, pit_handler);
}
