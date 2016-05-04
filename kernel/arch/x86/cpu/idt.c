#include <core/system.h>

struct idt_entry
{
	uint32_t offset_lo	: 16;	/* offset 0:15 */
	uint32_t selector	: 16;	/* Code segment selector */
	uint32_t __unused	: 8;	/* Unused, should be set to 0 */
	uint32_t flags		: 5;	/* Always set to 01110 */
	uint32_t dpl		: 2;	/* Descriptor privellage level */
	uint32_t p 			: 1;	/* Present */
	uint16_t offset_hi	: 16;	/* offset 16:31 */
} __attribute__((packed)) idt[256] = {0};

struct idt_ptr
{
	uint16_t limit;
	uint32_t base;
} __attribute__((packed)) 
idt_pointer = 
{
	sizeof(struct idt_entry) * 256 - 1,
	(uint32_t) &idt
};

#define DPL0 0
#define DPL3 3

/* Sets Interrupt gates in Kernel Code Segment */
void x86_idt_set_gate(uint32_t id, uint32_t offset)
{
	idt[id].offset_lo = (offset >> 0x00) & 0xFFFF;
	idt[id].offset_hi = (offset >> 0x10) & 0xFFFF;

	idt[id].selector = 0x8;
	idt[id].p = 1;
	idt[id].dpl = DPL0;
	idt[id].flags = 0x0E;
}

/* Sets Interrupt gates in User Code Segment */
void x86_idt_set_gate_user(uint32_t id, uint32_t offset)
{
	idt[id].offset_lo = (offset >> 0x00) & 0xFFFF;
	idt[id].offset_hi = (offset >> 0x10) & 0xFFFF;

	idt[id].selector = 0x8;
	idt[id].p = 1;
	idt[id].dpl = DPL3;
	idt[id].flags = 0x0E;
}

void x86_idt_setup()
{
	asm volatile("lidtl (%0)"::"g"(idt_pointer));
}
