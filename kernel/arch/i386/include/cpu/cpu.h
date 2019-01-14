#ifndef _X86_CPU_H
#define _X86_CPU_H

#include <core/system.h>
#include <cpu/cpuid.h>

struct x86_regs {
#if ARCH_BITS==32
    uint32_t
    edi, esi, ebp, ebx, ecx, edx, eax,
    eip, cs, eflags, esp, ss;
#else
    uint64_t
    r15, r14, r13, r12, r11, r10, r9, r8,
    rdi, rsi, rbp, rbx, rcx, rdx, rax,
    rip, cs, rflags, rsp, ss;
#endif
};

static inline void x86_dump_registers(struct x86_regs *regs)
{
    printk("Registers dump:\n");
#if ARCH_BITS==32
    printk("edi = %p\n", regs->edi);
    printk("esi = %p\n", regs->esi);
    printk("ebp = %p\n", regs->ebp);
    printk("ebx = %p\n", regs->ebx);
    printk("ecx = %p\n", regs->ecx);
    printk("edx = %p\n", regs->edx);
    printk("eax = %p\n", regs->eax);
    printk("eip = %p\n", regs->eip);
    printk("cs  = %p\n", regs->cs );
    printk("eflags = %p\n", regs->eflags);
    printk("esp = %p\n", regs->esp);
    printk("ss  = %p\n", regs->ss);
#else
    printk("r15 = %p\n", regs->r15);
    printk("r14 = %p\n", regs->r14);
    printk("r13 = %p\n", regs->r13);
    printk("r12 = %p\n", regs->r12);
    printk("r11 = %p\n", regs->r11);
    printk("r10 = %p\n", regs->r10);
    printk("r9  = %p\n", regs->r9);
    printk("r8  = %p\n", regs->r8);
    printk("rdi = %p\n", regs->rdi);
    printk("rsi = %p\n", regs->rsi);
    printk("rbp = %p\n", regs->rbp);
    printk("rbx = %p\n", regs->rbx);
    printk("rcx = %p\n", regs->rcx);
    printk("rdx = %p\n", regs->rdx);
    printk("rax = %p\n", regs->rax);
    printk("rip = %p\n", regs->rip);
    printk("cs  = %p\n", regs->cs );
    printk("rflags = %p\n", regs->rflags);
    printk("rsp = %p\n", regs->rsp);
    printk("ss  = %p\n", regs->ss);
#endif
}

struct x86_cpu {
    int id;
    union  x86_cpuid_vendor   vendor;
    //struct x86_cpuid_features features;
};

/* CR0 */
#define CR0_PG  _BV(31)
#define CR0_MP  _BV(1)
#define CR0_EM  _BV(2)
#define CR0_NE  _BV(5)

/* CR4 */
#define CR4_PSE _BV(4)

/* CPU function */
static inline uintptr_t read_cr0(void)
{
    uint32_t retval = 0;
#if ARCH_BITS==32
    asm volatile("mov %%cr0, %%eax":"=a"(retval));
#else
    asm volatile("movq %%cr0, %%rax":"=a"(retval));
#endif
    return retval;
}

static inline uintptr_t read_cr1(void)
{
    uintptr_t retval = 0;
#if ARCH_BITS==32
    asm volatile("mov %%cr1, %%eax":"=a"(retval));
#else
    asm volatile("movq %%cr1, %%rax":"=a"(retval));
#endif
    return retval;
}

static inline uintptr_t read_cr2(void)
{
    uintptr_t retval = 0;
#if ARCH_BITS==32
    asm volatile("mov %%cr2, %%eax":"=a"(retval));
#else
    asm volatile("movq %%cr2, %%rax":"=a"(retval));
#endif
    return retval;
}

static inline uintptr_t read_cr3(void)
{
    uintptr_t retval = 0;
#if ARCH_BITS==32
    asm volatile("mov %%cr3, %%eax":"=a"(retval));
#else
    asm volatile("movq %%cr3, %%rax":"=a"(retval));
#endif
    return retval;
}

static inline uintptr_t read_cr4(void)
{
    uintptr_t retval = 0;
#if ARCH_BITS==32
    asm volatile("mov %%cr4, %%eax":"=a"(retval));
#else
    asm volatile("movq %%cr4, %%rax":"=a"(retval));
#endif
    return retval;
}

static inline void write_cr0(uintptr_t val)
{
#if ARCH_BITS==32
    asm volatile("mov %%eax, %%cr0"::"a"(val));
#else
    asm volatile("movq %%rax, %%cr0"::"a"(val));
#endif
}

static inline void write_cr1(uintptr_t val)
{
#if ARCH_BITS==32
    asm volatile("mov %%eax, %%cr1"::"a"(val));
#else
    asm volatile("movq %%rax, %%cr1"::"a"(val));
#endif
}

static inline void write_cr2(uintptr_t val)
{
#if ARCH_BITS==32
    asm volatile("mov %%eax, %%cr2"::"a"(val));
#else
    asm volatile("movq %%rax, %%cr2"::"a"(val));
#endif
}

static inline void write_cr3(uintptr_t val)
{
#if ARCH_BITS==32
    asm volatile("mov %%eax, %%cr3"::"a"(val));
#else
    asm volatile("movq %%rax, %%cr3"::"a"(val));
#endif
}

static inline void write_cr4(uintptr_t val)
{
#if ARCH_BITS==32
    asm volatile("mov %%eax, %%cr4"::"a"(val));
#else
    asm volatile("movq %%rax, %%cr4"::"a"(val));
#endif
}

/* cpu/gdt.c */
void x86_gdt_setup(void);
void x86_tss_setup(uintptr_t sp);
void x86_kernel_stack_set(uintptr_t sp);

/* cpu/idt.c */
void x86_idt_setup(void);
void x86_idt_gate_set(uint32_t id, uintptr_t offset);
void x86_idt_gate_user_set(uint32_t id, uintptr_t offset);

/* cpu/isr.c */
void x86_isr_setup(void);

/* cpu/fpu.c */
void x86_fpu_enable(void);
void x86_fpu_disable(void);
void x86_fpu_trap(void);

//void pic_setup(void);
//void pic_disable(void);
//void pit_setup(uint32_t);
//void acpi_setup(void);
//uintptr_t acpi_rsdt_find(char signature[4]);
//void hpet_setup(void);
//int hpet_timer_setup(size_t period_ns, void (*handler)());

//#include "msr.h"
//#include "sdt.h"
//#include "pit.h"

#include_next <cpu/cpu.h>

#endif /* ! _X86_CPU_H */
