#ifndef _PLATFORM_MISC_H
#define _PLATFORM_MISC_H

#include <cpu/cpu.h>
#include <cpu/io.h>
#include <dev/irq.h>

/* x86: the PC's interrupt controllers, timers and firmware tables
 * (platform/misc/). */

/* irq.c: IRQ n is vector 32 + n; the local APIC's timer is an IRQ apart */
#define X86_IRQ_NR          49
#define X86_IRQ_TIMER       48
#define X86_TIMER_VECTOR    0xF0
#define X86_TLB_VECTOR      0xF1    /* an IPI: flush the TLB (mm/tlb.c) */
#define X86_WAKE_VECTOR     0xF2    /* an IPI: enter the kernel (arch_cpu_kick) */
#define X86_SPURIOUS_VECTOR 0xFF

/** An interrupt controller: the 8259s, or the APIC. */
struct x86_irq_chip {
    const char *name;
    void (*mask)(unsigned irq);
    void (*unmask)(unsigned irq);
    void (*eoi)(unsigned irq);
    const char *(*describe)(unsigned irq);  /* for /proc/interrupts */
    const char *timer;                      /* the tick's timer, if it has its own */
};

void x86_irq_chip_set(struct x86_irq_chip *chip);
const char *x86_irq_chip_name(void);
void x86_irq_gates_setup(void);
void x86_irq_tick_switch(void);
void x86_irq_alloc_from(unsigned first);
int  x86_irq_alloc(struct x86_irq_chip *chip);

/* pic.c: the 8259s */
int  x86_pic_setup(struct ioaddr *master, struct ioaddr *slave);
void x86_pic_disable(void);
extern struct x86_irq_chip x86_pic_chip;

/* apic.c: the local and I/O APICs; x86_apic_setup() returns 0 if the
 * APIC brings the interrupts */
int  x86_apic_setup(void) __attribute__((weak));
uint32_t x86_lapic_timer_setup(uint32_t period_ns, irq_handler_t handler) __attribute__((weak));
void x86_lapic_cpu_setup(void);
int  x86_apic_cpus(uint32_t *ids, int max);
void x86_lapic_ipi(uint32_t id, uint32_t icr);
void x86_lapic_eoi(void);
void x86_lapic_timer_cpu_start(void);

/* cpu/smp.c: starts the other CPUs (with the APIC) */
void x86_smp_init(void);

/* sys/clock.c: whether the clock counts finer than the ticks (the TSC or
 * the HPET) */
int x86_clock_precise(void);

/* acpi.c (if it's built) */
void x86_acpi_poweroff(void) __attribute__((weak));
void *x86_acpi_table(const char *signature) __attribute__((weak));

/* hpet.c (if it's built) */
int x86_hpet_init(void) __attribute__((weak));
uint64_t x86_hpet_read(void) __attribute__((weak));
uint32_t x86_hpet_read32(void) __attribute__((weak));
uint64_t x86_hpet_period_fs(void) __attribute__((weak));

/* pit.c: the 8254 timer */
int x86_pit_setup(struct ioaddr *io);
uint32_t x86_pit_period_set(uint32_t period_ns);
void x86_pit_delay(uint32_t us);

/* cmos.c: the real time clock */
int x86_cmos_setup(struct ioaddr *ioaddr);

#endif /* ! _PLATFORM_MISC_H */
