#ifndef _PLATFORM_MISC_H
#define _PLATFORM_MISC_H

#include <cpu/cpu.h>
#include <cpu/io.h>

typedef void (*x86_irq_handler_t)(struct x86_regs *r);

int  x86_pic_setup(struct ioaddr *master, struct ioaddr *slave);
void x86_pic_disable(void);
void x86_irq_handler_install(unsigned irq, x86_irq_handler_t handler);
void x86_irq_handler_uninstall(unsigned irq);

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
