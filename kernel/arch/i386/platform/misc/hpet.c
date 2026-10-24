/** \file
 * x86: the High Precision Event Timer (HPET), as a clock.
 *
 * Only the HPET's main counter is used, as a clock (sys/clock.c). ACPI's
 * HPET table says where its registers are, which are mapped uncached. It
 * counts at the period its capabilities say (10 ns on QEMU). "Legacy
 * replacement" is left off, so the PIT keeps IRQ 0, and the comparators
 * aren't used.
 *
 * References:
 *      [1] - IA-PC HPET (High Precision Event Timers) Specification,
 *            Intel, 1.0a
 */

#include <core/system.h>
#include <core/arch.h>
#include <cpu/sdt.h>
#include <platform/misc.h>

#define HPET_CAP        0x000   /* its high half is the period, in fs */
#define HPET_CNF        0x010
#define HPET_CNT        0x0F0

#define CNF_ENABLE      0x1
#define CNF_LEGACY      0x2

#define PERIOD_MAX_FS   100000000ULL    /* 100 ns, the most the specification allows */

static volatile uint32_t *regs;
static uint64_t period_fs;

/** Reads a 32-bit HPET register. */
static inline uint32_t reg32(unsigned off)
{
    return regs[off / 4];
}

/** Writes a 32-bit HPET register. */
static inline void reg32_write(unsigned off, uint32_t val)
{
    regs[off / 4] = val;
}

/**
 * Reads the HPET's 64-bit counter. Its two halves are read again until
 * the high half is the same before and after, so that a carry between
 * them can't give a wrong value.
 *
 * @return the counter
 */
uint64_t x86_hpet_read(void)
{
    uint32_t hi, lo;

    do {
        hi = reg32(HPET_CNT + 4);
        lo = reg32(HPET_CNT);
    } while (hi != reg32(HPET_CNT + 4));

    return (uint64_t) hi << 32 | lo;
}

/**
 * Reads the low half of the HPET's counter only: a single read, which
 * matters because each read is slow under a hypervisor.
 *
 * @return the counter's low 32 bits
 */
uint32_t x86_hpet_read32(void)
{
    return reg32(HPET_CNT);
}

/**
 * Returns the HPET counter's period.
 *
 * @return the period, in femtoseconds
 */
uint64_t x86_hpet_period_fs(void)
{
    return period_fs;
}

/**
 * Finds the HPET and starts its counter.
 *
 * @return 0 once it counts, `-ENODEV` if there's no usable HPET, or
 *         `-ENOMEM` if its registers couldn't be mapped
 */
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

    /* start counting from 0, leaving the PIT as it is */
    reg32_write(HPET_CNF, reg32(HPET_CNF) & ~(CNF_ENABLE | CNF_LEGACY));
    reg32_write(HPET_CNT, 0);
    reg32_write(HPET_CNT + 4, 0);
    reg32_write(HPET_CNF, (reg32(HPET_CNF) & ~CNF_LEGACY) | CNF_ENABLE);

    printk("hpet: at %p, %ld fs a count\n", (void *) (uintptr_t) t->address.address, period_fs);
    return 0;
}
