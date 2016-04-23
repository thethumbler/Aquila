#ifndef _X86_CPU_H
#define _X86_CPU_H

struct cpu_features
{
	int fpu   :1;	/* onboard x87 FPU */
	int vme   :1;	/* Virtual 8086 Mode Extensions */
	int de    :1;	/* Debugging Extensions */
	int pse   :1;	/* Page Size Extensions */
	int tsc   :1;	/* Time Stamp Counter */
	int msr   :1;	/* Mode Specific Regisgers */
	int pae   :1;	/* Physical Address Extensions */
	int mce   :1;	/* Machine Check Exceptions */
	int cx8   :1;	/* CMPXCHG8 (compare and swap) instruction support */
	int apic  :1;	/* onboard Advanced Programmable Interrupt Controller */
	int _res  :1;	/* reserved */
	int sep   :1;	/* SYSENTER and SYSEXIT instructions */
	int mtrr  :1;	/* Memory Type Range Registers */
	int pge   :1;	/* Page Global Enable bit in CR4 support */
	int mca   :1;	/* Machine Check Architecture */
	int cmov  :1;	/* Conditional MOV and FCMOV instructions support */
	int pat   :1;	/* Page Attribute Table */
	int pse36 :1;	/* 36-bit Page Size Extension */
	int psn   :1;	/* Processor Serial Number */
	int clfsh :1;	/* CLFluSH instruction (SSE2) support */
	int __res :1;	/* reserved */
	int ds    :1;	/* Debug Store: save trace of executed jumps */
	int acpi  :1;	/* onboard thermal contorl MSRs for ACPI */
	int mmx   :1;	/* MMX instructions support */
	int fxsr  :1;	/* FXSAVE, FXRESTOR instruction support */
	int sse   :1;	/* SSE instruction support */
	int sse2  :1;	/* SSE2 instruction support */
	int ss    :1;	/* CPU cache Self-Snoop support */
	int htt   :1;	/* Hyper-Threading Technology support */
	int tm    :1;	/* Thermal Monitor support */
	int ia64  :1;	/* IA64 processor emulating x86 */
	int pbe   :1;	/* Pending Break Enable */
}__attribute__((packed));

typedef struct
{
	uint32_t
	edi, esi, ebp, ebx, ecx, edx, eax,
	eip, cs, eflags, esp;
} __attribute__((packed)) regs_t;

/* TODO: Move these declrations to some specific file */
struct cpu_features *x86_get_features();
int x86_get_cpu_count();
void x86_gdt_setup();
void x86_idt_setup();
void x86_idt_set_gate(uint32_t id, uint32_t offset);
void x86_isr_setup();
void x86_pic_setup();
void x86_pic_disable();
void x86_pit_setup(uint32_t);

#include "msr.h"
#include "sdt.h"
#include "irq.h"

#endif /* !_X86_CPU_H */