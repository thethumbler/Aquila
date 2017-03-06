#ifndef _X86_CPU_H
#define _X86_CPU_H

struct cpu_features {
	int fpu   : 1;	/* onboard x87 FPU */
	int vme   : 1;	/* Virtual 8086 Mode Extensions */
	int de    : 1;	/* Debugging Extensions */
	int pse   : 1;	/* Page Size Extensions */
	int tsc   : 1;	/* Time Stamp Counter */
	int msr   : 1;	/* Mode Specific Regisgers */
	int pae   : 1;	/* Physical Address Extensions */
	int mce   : 1;	/* Machine Check Exceptions */
	int cx8   : 1;	/* CMPXCHG8 (compare and swap) instruction support */
	int apic  : 1;	/* onboard Advanced Programmable Interrupt Controller */
	int _res  : 1;	/* reserved */
	int sep   : 1;	/* SYSENTER and SYSEXIT instructions */
	int mtrr  : 1;	/* Memory Type Range Registers */
	int pge   : 1;	/* Page Global Enable bit in CR4 support */
	int mca   : 1;	/* Machine Check Architecture */
	int cmov  : 1;	/* Conditional MOV and FCMOV instructions support */
	int pat   : 1;	/* Page Attribute Table */
	int pse36 : 1;	/* 36-bit Page Size Extension */
	int psn   : 1;	/* Processor Serial Number */
	int clfsh : 1;	/* CLFluSH instruction (SSE2) support */
	int __res : 1;	/* reserved */
	int ds    : 1;	/* Debug Store: save trace of executed jumps */
	int acpi  : 1;	/* onboard thermal contorl MSRs for ACPI */
	int mmx   : 1;	/* MMX instructions support */
	int fxsr  : 1;	/* FXSAVE, FXRESTOR instruction support */
	int sse   : 1;	/* SSE instruction support */
	int sse2  : 1;	/* SSE2 instruction support */
	int ss    : 1;	/* CPU cache Self-Snoop support */
	int htt   : 1;	/* Hyper-Threading Technology support */
	int tm    : 1;	/* Thermal Monitor support */
	int ia64  : 1;	/* IA64 processor emulating x86 */
	int pbe   : 1;	/* Pending Break Enable */
} __packed;

typedef struct {
	uint32_t
	edi, esi, ebp, ebx, ecx, edx, eax,
	eip, cs, eflags, esp, ss;
} __packed regs_t;

static inline void x86_dump_registers(regs_t *regs)
{
	printk("Registers dump:\n");
	printk("edi = %x\n", regs->edi);
	printk("esi = %x\n", regs->esi);
	printk("ebp = %x\n", regs->ebp);
	printk("ebx = %x\n", regs->ebx);
	printk("ecx = %x\n", regs->ecx);
	printk("edx = %x\n", regs->edx);
	printk("eax = %x\n", regs->eax);
	printk("eip = %x\n", regs->eip);
	printk("cs  = %x\n", regs->cs );
	printk("eflags = %x\n", regs->eflags);
	printk("esp = %x\n", regs->esp);
	printk("ss  = %x\n", regs->ss);
}

struct cpu {
	int id;
	struct cpu_features features;
};


/* CR0 */
#define CR0_PG	_BV(31)

/* CR4 */
#define CR4_PSE	_BV(4)

/* CPU function */
static inline uint32_t read_cr0()
{
	uint32_t retval = 0;
	asm volatile("mov %%cr0, %%eax":"=a"(retval));
	return retval;
}

static inline uint32_t read_cr1()
{
	uint32_t retval = 0;
	asm volatile("mov %%cr1, %%eax":"=a"(retval));
	return retval;
}

static inline uint32_t read_cr2()
{
	uint32_t retval = 0;
	asm volatile("mov %%cr2, %%eax":"=a"(retval));
	return retval;
}

static inline uint32_t read_cr3()
{
	uint32_t retval = 0;
	asm volatile("mov %%cr3, %%eax":"=a"(retval));
	return retval;
}

static inline uint32_t read_cr4()
{
	uint32_t retval = 0;
	asm volatile("mov %%cr4, %%eax":"=a"(retval));
	return retval;
}

static inline void write_cr0(uint32_t val)
{
	asm volatile("mov %%eax, %%cr0"::"a"(val));
}

static inline void write_cr1(uint32_t val)
{
	asm volatile("mov %%eax, %%cr1"::"a"(val));
}

static inline void write_cr2(uint32_t val)
{
	asm volatile("mov %%eax, %%cr2"::"a"(val));
}

static inline void write_cr3(uint32_t val)
{
	asm volatile("mov %%eax, %%cr3"::"a"(val));
}

static inline void write_cr4(uint32_t val)
{
	asm volatile("mov %%eax, %%cr4"::"a"(val));
}

static inline int check_cpuid()
{
	int retval = 0;
	asm volatile(
		"pushf \n"
		"pushf \n"
		"xorl $1 << 21, (%%esp) \n"
		"popf \n"
		"pushf \n"
		"pop %%eax \n"
		"xorl (%%esp), %%eax \n"
		"andl $1 << 21, %%eax \n"
		"popf \n"
	:"=a"(retval));
	return retval;
}

static inline struct cpu_features get_cpu_features()
{
	struct cpu_features features;
	if (check_cpuid()) {
		asm volatile ("cpuid":"=b"((int){0}), "=c"((int){0}), "=d"(features):"a"(1));
	} else {
		/* CPUID is not supported -- fall back to i386\n */
		/* TODO */
	}
	return features;
}

union vendor_id {
	char string[13];
	uint32_t array[3];
} __packed;

static inline union vendor_id get_vendor_id()
{
	union vendor_id vendor_id;
	asm volatile("cpuid":
		"=b"(vendor_id.array[0]),
		"=d"(vendor_id.array[1]),
		"=c"(vendor_id.array[2])
		:"a"(0));
	vendor_id.string[12] = 0;
	return vendor_id;
}

/* TODO: Move these declrations to some specific file */
int get_cpus_count();
void gdt_setup();
void idt_setup();
void idt_set_gate(uint32_t id, uint32_t offset);
void idt_set_gate_user(uint32_t id, uint32_t offset);
void isr_setup();
void pic_setup();
void pic_disable();
void pit_setup(uint32_t);
void set_tss_esp(uint32_t esp);
void set_kernel_stack(uintptr_t esp);

#include "msr.h"
#include "sdt.h"
#include "irq.h"
#include "pit.h"

#endif /* !_X86_CPU_H */
