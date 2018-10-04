#ifndef _X86_CPU_H
#define _X86_CPU_H

#include <core/panic.h>

struct cpu_features {
    int fpu   : 1;  /* onboard x87 FPU */
    int vme   : 1;  /* Virtual 8086 Mode Extensions */
    int de    : 1;  /* Debugging Extensions */
    int pse   : 1;  /* Page Size Extensions */
    int tsc   : 1;  /* Time Stamp Counter */
    int msr   : 1;  /* Mode Specific Regisgers */
    int pae   : 1;  /* Physical Address Extensions */
    int mce   : 1;  /* Machine Check Exceptions */
    int cx8   : 1;  /* CMPXCHG8 (compare and swap) instruction support */
    int apic  : 1;  /* onboard Advanced Programmable Interrupt Controller */
    int _res  : 1;  /* reserved */
    int sep   : 1;  /* SYSENTER and SYSEXIT instructions */
    int mtrr  : 1;  /* Memory Type Range Registers */
    int pge   : 1;  /* Page Global Enable bit in CR4 support */
    int mca   : 1;  /* Machine Check Architecture */
    int cmov  : 1;  /* Conditional MOV and FCMOV instructions support */
    int pat   : 1;  /* Page Attribute Table */
    int pse36 : 1;  /* 36-bit Page Size Extension */
    int psn   : 1;  /* Processor Serial Number */
    int clfsh : 1;  /* CLFluSH instruction (SSE2) support */
    int __res : 1;  /* reserved */
    int ds    : 1;  /* Debug Store: save trace of executed jumps */
    int acpi  : 1;  /* onboard thermal contorl MSRs for ACPI */
    int mmx   : 1;  /* MMX instructions support */
    int fxsr  : 1;  /* FXSAVE, FXRESTOR instruction support */
    int sse   : 1;  /* SSE instruction support */
    int sse2  : 1;  /* SSE2 instruction support */
    int ss    : 1;  /* CPU cache Self-Snoop support */
    int htt   : 1;  /* Hyper-Threading Technology support */
    int tm    : 1;  /* Thermal Monitor support */
    int ia64  : 1;  /* IA64 processor emulating x86 */
    int pbe   : 1;  /* Pending Break Enable */
} __packed;

struct x86_regs {
#if ARCH_BITS==32
    uint32_t
    edi, esi, ebp, ebx, ecx, edx, eax,
    eip, cs, eflags, esp, ss;
#else
    uint64_t
    edi, esi, ebp, ebx, ecx, edx, eax,
    eip, cs, eflags, esp, ss;
#endif
} __packed;

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
    printk("rdi = %p\n", regs->edi);
    printk("rsi = %p\n", regs->esi);
    printk("rbp = %p\n", regs->ebp);
    printk("rbx = %p\n", regs->ebx);
    printk("rcx = %p\n", regs->ecx);
    printk("rdx = %p\n", regs->edx);
    printk("rax = %p\n", regs->eax);
    printk("rip = %p\n", regs->eip);
    printk("cs  = %p\n", regs->cs );
    printk("rflags = %p\n", regs->eflags);
    printk("rsp = %p\n", regs->esp);
    printk("ss  = %p\n", regs->ss);
#endif
}

struct cpu {
    int id;
    struct cpu_features features;
};


/* CR0 */
#define CR0_PG  _BV(31)
#define CR0_MP  _BV(1)
#define CR0_EM  _BV(2)
#define CR0_NE  _BV(5)

/* CR4 */
#define CR4_PSE _BV(4)

/* CPU function */
static inline uintptr_t read_cr0()
{
    uint32_t retval = 0;
#if ARCH_BITS==32
    asm volatile("mov %%cr0, %%eax":"=a"(retval));
#else
    asm volatile("movq %%cr0, %%rax":"=a"(retval));
#endif
    return retval;
}

static inline uintptr_t read_cr1()
{
    uintptr_t retval = 0;
#if ARCH_BITS==32
    asm volatile("mov %%cr1, %%eax":"=a"(retval));
#else
    asm volatile("movq %%cr1, %%rax":"=a"(retval));
#endif
    return retval;
}

static inline uintptr_t read_cr2()
{
    uintptr_t retval = 0;
#if ARCH_BITS==32
    asm volatile("mov %%cr2, %%eax":"=a"(retval));
#else
    asm volatile("movq %%cr2, %%rax":"=a"(retval));
#endif
    return retval;
}

static inline uintptr_t read_cr3()
{
    uintptr_t retval = 0;
#if ARCH_BITS==32
    asm volatile("mov %%cr3, %%eax":"=a"(retval));
#else
    asm volatile("movq %%cr3, %%rax":"=a"(retval));
#endif
    return retval;
}

static inline uintptr_t read_cr4()
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

static inline int check_cpuid()
{
#if ARCH_BITS==32
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
#else
    /* TODO */
    return 0;
#endif
}

static inline struct cpu_features *get_cpu_features(struct cpu_features *features)
{
    if (check_cpuid()) {
        asm volatile ("cpuid":"=b"((int){0}), "=c"((int){0}), "=d"(*features):"a"(1):"memory");
    } else {
        /* CPUID is not supported -- fall back to i386\n */
        /* TODO */
        panic("i386 Support not implemented");
    }
    return features;
}

union vendor_id {
    char string[13];
    uint32_t array[3];
} __packed;

static inline union vendor_id cpu_get_vendor_id()
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

/* cpu/gdt.c */
void x86_gdt_setup();
void x86_tss_sp_set(uintptr_t sp);
void x86_kernel_stack_set(uintptr_t sp);

/* cpu/idt.c */
void x86_idt_setup();
#if 0
void x86_idt_gate_set(uint32_t id, uint32_t offset);
void x86_idt_gate_user_set(uint32_t id, uint32_t offset);
#else
void x86_idt_gate_set(uint32_t id, uintptr_t offset);
void x86_idt_gate_user_set(uint32_t id, uintptr_t offset);
#endif

/* cpu/isr.c */
void x86_isr_setup();

void pic_setup();
void pic_disable();
void pit_setup(uint32_t);
void enable_fpu();
void disable_fpu();
void trap_fpu();
void acpi_setup();
uintptr_t acpi_rsdt_find(char signature[4]);
void hpet_setup();
int hpet_timer_setup(size_t period_ns, void (*handler)());

#include "msr.h"
#include "sdt.h"
#include "pit.h"

#endif /* !_X86_CPU_H */
