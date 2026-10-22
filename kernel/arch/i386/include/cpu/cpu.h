#ifndef _X86_CPU_H
#define _X86_CPU_H

#include <core/system.h>
#include <cpu/cpuid.h>

struct x86_regs {
#if ARCH_BITS==32
    uint32_t
    edi, esi, ebp, ebx, ecx, edx, eax,
    gs, fs, es, ds,
    vec, err,       /* the stub's: the vector, the error code (or 0) */
    eip, cs, eflags, esp, ss;
#else
    uint64_t
    r15, r14, r13, r12, r11, r10, r9, r8,
    rdi, rsi, rbp, rbx, rcx, rdx, rax,
    vec, err,       /* the stub's: the vector, the error code (or 0) */
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
    printk("gs  = %p\n", regs->gs );
    printk("fs  = %p\n", regs->fs );
    printk("es  = %p\n", regs->es );
    printk("ds  = %p\n", regs->ds );
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

/* a segment descriptor */
struct x86_gdt_entry {
    uint32_t limit_lo : 16; /* Segment Limit 15:00 */
    uint32_t base_lo  : 16; /* Base Address 15:00 */

    uint32_t base_mid : 8;  /* Base Address 23:16 */
    uint32_t type     : 4;  /* Segment Type */
    uint32_t s        : 1;  /* Descriptor type (0=system, 1=code) */
    uint32_t dpl      : 2;  /* Descriptor Privellage Level */
    uint32_t p        : 1;  /* Segment present */

    uint32_t limit_hi : 4;  /* Segment Limit 19:16 */
    uint32_t avl      : 1;  /* Avilable for use by system software */
    uint32_t l        : 1;  /* Long mode segment (64-bit only) */
    uint32_t db       : 1;  /* Default operation size / upper Bound */
    uint32_t g        : 1;  /* Granularity */
    uint32_t base_hi  : 8;  /* Base Address 31:24 */
} __aligned(8);

/* the task state segment: only the stack interrupts from user mode take */
#if ARCH_BITS==32
struct x86_tss {
    uint32_t link;
    uint32_t sp;
    uint32_t ss;
    uint32_t _[23]; /* To know the actuall fields, consult Intel Manuals */
} __packed;
#else
struct x86_tss {
    uint32_t _;
    uint64_t sp;
    uint32_t __[23]; /* To know the actuall fields, consult Intel Manuals */
} __packed;
#endif

/*
 * The GDT, each CPU's: the same segments, but its own TSS (a TSS is busy
 * on the CPU that loaded it), and on i386 the segment %gs selects in the
 * kernel, its struct cpu
 */
#define X86_GDT_TSS     5   /* 5 and 6 on x86_64 */
#define X86_GDT_CPU     7
#define X86_GDT_NR      8

#define X86_SEL_CPU     (X86_GDT_CPU << 3)

struct pmap;
struct thread;

/* the CPU's own (struct cpu's arch) */
struct arch_cpu {
    uint32_t apic_id;           /* its local APIC's */
    struct x86_gdt_entry gdt[X86_GDT_NR];
    struct x86_tss tss __aligned(8);
    struct pmap *pmap;          /* CR3's (pmap_switch) */
    struct thread *fpu_thread;  /* whose state the FPU has */
    int tick_switch;            /* the tick asked for a switch (irq.c) */
    uintptr_t idle_stack;       /* the idle loop's, its top */
    uint64_t tlb_flushes;       /* done for another CPU (mm/tlb.c) */
    uint64_t kernel_gen;        /* its TLB has the kernel's mappings as of */
};

/* the running CPU's struct cpu: its first member points to it, at %gs:0
 * (i386: the X86_SEL_CPU segment; x86_64: the GS base, user mode's
 * swapped in and out with swapgs) */
struct cpu;

static inline struct cpu *curcpu(void)
{
    struct cpu *cpu;
#if ARCH_BITS==32
    asm volatile("movl %%gs:0, %0" : "=r"(cpu));
#else
    asm volatile("movq %%gs:0, %0" : "=r"(cpu));
#endif
    return cpu;
}

/* CR0 */
#define CR0_PG  _BV(31)
#define CR0_WP  _BV(16)     /* honour read-only pages in kernel mode */
#define CR0_MP  _BV(1)
#define CR0_EM  _BV(2)
#define CR0_TS  _BV(3)
#define CR0_NE  _BV(5)

/* CR4 */
#define CR4_PSE _BV(4)
#define CR4_OSFXSR      _BV(9)
#define CR4_OSXMMEXCPT  _BV(10)

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
void x86_gdt_setup(struct cpu *cpu, uintptr_t sp);
void x86_kernel_stack_set(uintptr_t sp);

/* cpu/idt.c */
void x86_idt_setup(void);
void x86_idt_gate_set(uint32_t id, uintptr_t offset);
void x86_idt_gate_user_set(uint32_t id, uintptr_t offset);

/* cpu/isr.c */
void x86_isr_setup(void);

/* cpu/fpu.c */
void x86_fpu_setup(void);
void x86_fpu_enable(void);
void x86_fpu_disable(void);
void x86_fpu_trap(void);
int  x86_fpu_save_to(void *buf);
void x86_fpu_reset(void);
void x86_fpu_load_from(const void *buf, int valid);

/* sys/signal.c: back to user mode (its signals) from the context `r' */
void x86_return_to_user(struct x86_regs *r, int syscall);

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
