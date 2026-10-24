/** \file
 *          High Precision Event Timer (HPET)
 *
 * References:
 *      [1] - IA-PC HPET (High Precision Event Timers) Specification,
 *            Intel, 1.0a
 *
 * Its main counter only, as a clock (sys/clock.c): found from ACPI's HPET
 * table, its registers mapped uncached; counting at the period the
 * capabilities say (10 ns on QEMU), without "legacy replacement" (the PIT
 * keeps IRQ 0). Its comparators aren't used.
 */

#include <core/system.h>
#include <core/arch.h>
#include <cpu/sdt.h>
#include <platform/misc.h>

#define HPET_CAP        0x000   /* the period (fs) in the high half */
#define HPET_CNF        0x010
#define HPET_CNT        0x0F0

#define CNF_ENABLE      0x1
#define CNF_LEGACY      0x2

#define PERIOD_MAX_FS   100000000ULL    /* 100 ns, the specification's */

static volatile uint32_t *regs;
static uint64_t period_fs;

static inline uint32_t reg32(unsigned off)
{
    return regs[off / 4];
}

static inline void reg32_write(unsigned off, uint32_t val)
{
    regs[off / 4] = val;
}

/** the counter: its halves, read again until they agree */
uint64_t x86_hpet_read(void)
{
    uint32_t hi, lo;

    do {
        hi = reg32(HPET_CNT + 4);
        lo = reg32(HPET_CNT);
    } while (hi != reg32(HPET_CNT + 4));

    return (uint64_t) hi << 32 | lo;
}

/** its low half only: one read (each is slow, under a hypervisor) */
uint32_t x86_hpet_read32(void)
{
    return reg32(HPET_CNT);
}

uint64_t x86_hpet_period_fs(void)
{
    return period_fs;
}

/** 0 once it counts, -1 if there's no HPET */
int x86_hpet_init(void)
{
    struct acpi_hpet *t = x86_acpi_table ? x86_acpi_table("HPET") : NULL;

    if (!t || t->header.length < sizeof(*t) || t->address.space != 0 || !t->address.address)
        return -ENODEV;

    regs = arch_mmio_map(t->address.address, 1024);

    if (!regs)
        return -ENOMEM;

    period_fs = reg32(HPET_CAP + 4);

    if (!period_fs || period_fs > PERIOD_MAX_FS) {
        printk("hpet: bad period %ld fs\n", period_fs);
        return -ENODEV;
    }

    /* counting, from 0, the PIT left as it is */
    reg32_write(HPET_CNF, reg32(HPET_CNF) & ~(CNF_ENABLE | CNF_LEGACY));
    reg32_write(HPET_CNT, 0);
    reg32_write(HPET_CNT + 4, 0);
    reg32_write(HPET_CNF, (reg32(HPET_CNF) & ~CNF_LEGACY) | CNF_ENABLE);

    printk("hpet: at %p, %ld fs a count\n", (void *) (uintptr_t) t->address.address, period_fs);
    return 0;
}
