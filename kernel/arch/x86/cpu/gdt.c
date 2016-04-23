#include <core/system.h>

struct
{
    uint32_t link;
    uint32_t esp;
    uint32_t ss;
    uint32_t _[23]; /* To know the actuall fields, consult Intel Manuals */
} __attribute__((packed))  tss_entry;

#define TSS_BASE    ((uintptr_t)&tss_entry)
#define TSS_LIMIT   (sizeof(tss_entry))

#define RW_DATA	0x2
#define XR_CODE	0xA
#define TSS_AVL	0x9

#define BASE  0
#define LIMIT -1

#define DPL0 0
#define DPL3 3

struct gdt_entry
{
	uint32_t limit_lo : 16;	/* Segment Limit 15:00 */
	uint32_t base_lo  : 16;	/* Base Address 15:00 */
	uint32_t base_mid : 8;	/* Base Address 23:16 */
	uint32_t type     : 4;	/* Segment Type */
	uint32_t s        : 1;	/* Descriptor type (0=system, 1=code) */
	uint32_t dpl      : 2;	/* Descriptor Privellage Level */
	uint32_t p        : 1;	/* Segment present */
	uint32_t limit_hi : 4;	/* Segment Limit 19:16 */
	uint32_t avl      : 1;	/* Avilable for use by system software */
	uint32_t l        : 1;	/* Long mode segment (64-bit only) */
	uint32_t db       : 1;	/* Default operation size / upper Bound */
	uint32_t g        : 1;	/* Granularity */
	uint32_t base_hi  : 8;	/* Base Address 31:24 */
} __attribute__((packed, aligned(8)))
gdt[256] =
{
	/* Null Segment */
	{0},

	/* Code Segment - Kernel */
	{LIMIT, BASE, BASE, XR_CODE, 1, DPL0, 1, LIMIT, 0, 0, 1, 1, BASE},

	/* Data Segment - Kernel */
	{LIMIT, BASE, BASE, RW_DATA, 1, DPL0, 1, LIMIT, 0, 0, 1, 1, BASE},

	/* Code Segment - User */
	{LIMIT, BASE, BASE, XR_CODE, 1, DPL3, 1, LIMIT, 0, 0, 1, 1, BASE},

	/* Data Segment - User */
	{LIMIT, BASE, BASE, RW_DATA, 1, DPL3, 1, LIMIT, 0, 0, 1, 1, BASE},
};

struct
{
	uint16_t size;
	uint32_t offset;
} __attribute__((packed, aligned(8))) gdt_pointer =
{
	sizeof(gdt) - 1,
	(uint32_t)gdt,
};

void x86_gdt_setup()
{
	asm volatile("\
	lgdtl (%0) \n\
	ljmp $0x8,$1f \n\
	1: \n\
	movl %%eax, %%ds \n\
	movl %%eax, %%es \n\
	movl %%eax, %%fs \n\
	movl %%eax, %%gs \n\
	movl %%eax, %%ss \n\
	"::"g"(gdt_pointer), "a"(0x10));
}

void x86_set_tss_esp(uint32_t esp)
{
    tss_entry = (typeof(tss_entry)){0};
    tss_entry.ss  = 0x10;
    tss_entry.esp = esp;
    /* TSS Segment */
	gdt[5] = (struct gdt_entry){TSS_LIMIT & 0xFFFF, TSS_BASE & 0xFFFF,
		(TSS_BASE >> 16) & 0xFF, TSS_AVL, 0, DPL3, 1,
		(TSS_LIMIT >> 16) & 0xF, 0, 0, 0, 0, (TSS_BASE >> 24 & 0xFF)};

    asm volatile ("ltr %%ax;"::"a"(0x28 | DPL3));
}
