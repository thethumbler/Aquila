#ifndef _PLATFORM_MISC_H
#define _PLATFORM_MISC_H

#include <cpu/cpu.h>
#include <cpu/io.h>

typedef void (*x86_irq_handler_t)(struct x86_regs *r);

/* irq.c: IRQ n is vector 32 + n; the local APIC's timer an IRQ apart */
#define X86_IRQ_NR          49
#define X86_IRQ_TIMER       48
#define X86_TIMER_VECTOR    0xF0
#define X86_SPURIOUS_VECTOR 0xFF

/* an interrupt controller: the 8259, or the APIC */
struct x86_irq_chip {
    const char *name;
    void (*mask)(unsigned irq);
    void (*unmask)(unsigned irq);
    void (*eoi)(unsigned irq);
    const char *(*describe)(unsigned irq);  /* for /proc/interrupts */
    const char *timer;                      /* the tick's, if it's its own */
};

void x86_irq_chip_set(struct x86_irq_chip *chip);
const char *x86_irq_chip_name(void);
void x86_irq_gates_setup(void);
void x86_irq_handler_install(unsigned irq, x86_irq_handler_t handler);
void x86_irq_handler_uninstall(unsigned irq);
void x86_irq_tick_switch(void);

/* pic.c: the 8259s */
int  x86_pic_setup(struct ioaddr *master, struct ioaddr *slave);
void x86_pic_disable(void);
extern struct x86_irq_chip x86_pic_chip;

/* lapic.c, ioapic.c: the APIC; 0 if it's what brings the interrupts */
int  x86_apic_setup(void) __attribute__((weak));
uint32_t x86_lapic_timer_setup(uint32_t period_ns, x86_irq_handler_t handler) __attribute__((weak));

/* sys/clock.c: it counts finer than the ticks (the TSC or the HPET) */
int x86_clock_precise(void);

int x86_i8042_setup(struct ioaddr *io);
void x86_i8042_handler_register(int channel, void (*fun)(int));
void x86_i8042_reboot(void);
int  x86_i8042_aux_write(uint8_t byte);

void x86_acpi_poweroff(void) __attribute__((weak));
void *x86_acpi_table(const char *signature) __attribute__((weak));

/* hpet.c (if it's built) */
int x86_hpet_init(void) __attribute__((weak));
uint64_t x86_hpet_read(void) __attribute__((weak));
uint32_t x86_hpet_read32(void) __attribute__((weak));
uint64_t x86_hpet_period_fs(void) __attribute__((weak));

int x86_pit_setup(struct ioaddr *io);
uint32_t x86_pit_period_set(uint32_t period_ns);
void x86_pit_delay(uint32_t us);

int x86_cmos_setup(struct ioaddr *ioaddr);

#endif /* ! _PLATFORM_MISC_H */
