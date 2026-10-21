/*
 *          The APIC: the local APIC and the I/O APICs
 *
 * References:
 *      [1] - Intel(R) 64 and IA-32 Architectures Software Developer's
 *            Manual, Volume 3, Chapter 11 (APIC)
 *      [2] - 82093AA I/O Advanced Programmable Interrupt Controller
 *            (IOAPIC), Intel
 *      [3] - ACPI Specification, 5.2.12 (MADT)
 *
 * An x86_irq_chip (irq.c), when the CPU has an APIC and ACPI's MADT says
 * where they are (apic=off keeps the 8259):
 *
 * - the local APIC (the boot CPU's): enabled (spurious vector 0xFF), its
 *   LINT0 (the 8259's) masked, LINT1 the NMI as the MADT says; its timer
 *   is the scheduler's tick, periodic, calibrated against the clock
 * - the I/O APICs: IRQ n goes to the pin the MADT's overrides say (ISA's,
 *   0 is 2 on most machines), edge and active high but as they say; the
 *   PCI lines (from the devices' configuration space) are overridden as
 *   level-triggered on QEMU's pc. Each pin goes to the boot CPU, vector
 *   32 + n, masked until a handler is installed
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/kargs.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <cpu/msr.h>
#include <cpu/sdt.h>
#include <platform/misc.h>

/* the local APIC's registers */
#define LAPIC_ID        0x020
#define LAPIC_TPR       0x080
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR 0x390
#define LAPIC_TIMER_DIV 0x3E0

#define SVR_ENABLE      0x100
#define LVT_MASKED      0x10000
#define LVT_NMI         0x400
#define LVT_PERIODIC    0x20000
#define TIMER_DIV_16    0x3

#define APIC_BASE_ENABLE    0x800

/* the I/O APIC's: a register selected, then read or written */
#define IOAPIC_SEL      0x00
#define IOAPIC_WIN      0x10
#define IOAPIC_VER      0x01
#define IOAPIC_REDIR(n) (0x10 + 2 * (n))

#define REDIR_MASKED    0x10000
#define REDIR_LEVEL     0x8000
#define REDIR_LOW       0x2000

#define IOAPICS_MAX     4

static volatile uint32_t *lapic;
static uint32_t lapic_id;

static struct ioapic {
    volatile uint32_t *regs;
    uint32_t address, gsi_base, pins;
} ioapics[IOAPICS_MAX];
static int ioapics_nr;

/* ISA IRQs: their GSI, and its flags (0: ISA's, edge and high) */
static struct {
    uint32_t gsi;
    uint16_t flags;
} isa[16];

/* each IRQ's route, for /proc/interrupts */
static struct {
    uint32_t gsi;
    int level;
} routes[X86_IRQ_NR];

static uint32_t timer_count;

static inline uint32_t lapic_read(unsigned reg)
{
    return lapic[reg / 4];
}

static inline void lapic_write(unsigned reg, uint32_t val)
{
    lapic[reg / 4] = val;
}

static struct ioapic *ioapic_of(uint32_t gsi)
{
    for (int i = 0; i < ioapics_nr; ++i)
        if (gsi >= ioapics[i].gsi_base && gsi < ioapics[i].gsi_base + ioapics[i].pins)
            return &ioapics[i];

    return NULL;
}

static uint32_t ioapic_read(struct ioapic *io, uint32_t reg)
{
    io->regs[IOAPIC_SEL / 4] = reg;
    return io->regs[IOAPIC_WIN / 4];
}

static void ioapic_write(struct ioapic *io, uint32_t reg, uint32_t val)
{
    io->regs[IOAPIC_SEL / 4] = reg;
    io->regs[IOAPIC_WIN / 4] = val;
}

/* IRQ n's pin, and how it's triggered */
static uint32_t irq_gsi(unsigned irq, int *level, int *low)
{
    uint16_t flags = irq < 16 ? isa[irq].flags : 0;
    uint32_t gsi = irq < 16 ? isa[irq].gsi : irq;

    /* ISA's (conforming): edge, active high; PCI's past 16: level, low */
    *level = (flags & ACPI_MPS_TRIGGER_MASK) ? (flags & ACPI_MPS_TRIGGER_MASK) == ACPI_MPS_TRIGGER_LEVEL : irq >= 16;
    *low = (flags & ACPI_MPS_POLARITY_MASK) ? (flags & ACPI_MPS_POLARITY_MASK) == ACPI_MPS_POLARITY_LOW : irq >= 16;

    return gsi;
}

static void apic_route(unsigned irq, int masked)
{
    int level, low;
    uint32_t gsi = irq_gsi(irq, &level, &low);
    struct ioapic *io = ioapic_of(gsi);

    if (!io) {
        printk("apic: IRQ %d: no I/O APIC has GSI %d\n", irq, gsi);
        return;
    }

    uint32_t pin = gsi - io->gsi_base;
    uint32_t lo = (32 + irq) | (level ? REDIR_LEVEL : 0) | (low ? REDIR_LOW : 0) |
                  (masked ? REDIR_MASKED : 0);

    ioapic_write(io, IOAPIC_REDIR(pin) + 1, lapic_id << 24);
    ioapic_write(io, IOAPIC_REDIR(pin), lo);

    routes[irq].gsi = gsi;
    routes[irq].level = level;
}

static void apic_mask(unsigned irq)
{
    if (irq < X86_IRQ_TIMER)
        apic_route(irq, 1);
}

static void apic_unmask(unsigned irq)
{
    if (irq < X86_IRQ_TIMER)
        apic_route(irq, 0);
}

static void apic_eoi(unsigned irq)
{
    lapic_write(LAPIC_EOI, 0);
}

static const char *apic_describe(unsigned irq)
{
    static char buf[32];

    snprintf(buf, sizeof(buf), "ioapic gsi %d %s", routes[irq].gsi,
             routes[irq].level ? "level" : "edge");
    return buf;
}

static struct x86_irq_chip apic_chip = {
    .name     = "apic",
    .mask     = apic_mask,
    .unmask   = apic_unmask,
    .eoi      = apic_eoi,
    .describe = apic_describe,
    .timer    = "lapic",
};

/* the CPU has a local APIC (CPUID 1, EDX bit 9) */
static int cpu_has_apic(void)
{
    uint32_t a, b, c, d;

    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return !!(d & (1 << 9));
}

/* 0 if the APIC brings the interrupts from now on */
int x86_apic_setup(void)
{
    const char *arg = NULL;

    if (!kargs_get("apic", &arg) && arg && !strcmp(arg, "off")) {
        printk("apic: off (apic=off)\n");
        return -1;
    }

    struct acpi_madt *madt = x86_acpi_table ? x86_acpi_table("APIC") : NULL;

    if (!cpu_has_apic() || !madt) {
        printk("apic: %s\n", madt ? "the CPU has none" : "no MADT");
        return -1;
    }

    /* the ISA IRQs: the same pins, but as the overrides say */
    for (int i = 0; i < 16; ++i) {
        isa[i].gsi = i;
        isa[i].flags = 0;
    }

    uint8_t *p = (uint8_t *) (madt + 1), *end = (uint8_t *) madt + madt->header.length;
    int nmi_lint = 1;

    for (; p + 2 <= end && p[1] >= 2; p += p[1]) {
        switch (p[0]) {
            case ACPI_MADT_IO_APIC: {
                struct acpi_madt_io_apic *e = (void *) p;

                if (ioapics_nr == IOAPICS_MAX)
                    break;

                struct ioapic *io = &ioapics[ioapics_nr];
                io->regs = arch_mmio_map(e->address, 4096);
                io->address = e->address;
                io->gsi_base = e->gsi_base;
                io->pins = ((ioapic_read(io, IOAPIC_VER) >> 16) & 0xff) + 1;
                ioapics_nr++;
                break;
            }
            case ACPI_MADT_INT_SRC_OVERRIDE: {
                struct acpi_madt_override *e = (void *) p;

                if (e->bus == 0 && e->source < 16) {
                    isa[e->source].gsi = e->gsi;
                    isa[e->source].flags = e->flags;
                }
                break;
            }
            case ACPI_MADT_LOCAL_APIC_NMI: {
                struct acpi_madt_local_apic_nmi *e = (void *) p;
                nmi_lint = e->lint;
                break;
            }
        }
    }

    if (!ioapics_nr) {
        printk("apic: no I/O APIC\n");
        return -1;
    }

    /* the local APIC: enabled, where the MADT says */
    msr_write(APIC_BASE, msr_read(APIC_BASE) | APIC_BASE_ENABLE);
    lapic = arch_mmio_map(madt->local_apic_address, 4096);
    lapic_id = lapic_read(LAPIC_ID) >> 24;

    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LVT_MASKED);
    lapic_write(nmi_lint ? LAPIC_LVT_LINT1 : LAPIC_LVT_LINT0, LVT_NMI);
    lapic_write(nmi_lint ? LAPIC_LVT_LINT0 : LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_SVR, SVR_ENABLE | X86_SPURIOUS_VECTOR);

    /* every pin masked, until there's a handler */
    for (int i = 0; i < ioapics_nr; ++i)
        for (uint32_t pin = 0; pin < ioapics[i].pins; ++pin)
            ioapic_write(&ioapics[i], IOAPIC_REDIR(pin), REDIR_MASKED);

    /* the 8259s, if there are any: masked, they'd come through LINT0 */
    x86_pic_disable();

    x86_irq_chip_set(&apic_chip);

    printk("apic: local APIC %d at %p, %d I/O APIC(s), the first at %p (GSI %d, %d pins)\n",
           lapic_id, (void *) (uintptr_t) madt->local_apic_address, ioapics_nr,
           (void *) (uintptr_t) ioapics[0].address, ioapics[0].gsi_base, ioapics[0].pins);

    for (int i = 0; i < 16; ++i)
        if (isa[i].gsi != (uint32_t) i || isa[i].flags)
            printk("apic: IRQ %d is GSI %d%s\n", i, isa[i].gsi,
                   (isa[i].flags & ACPI_MPS_TRIGGER_MASK) == ACPI_MPS_TRIGGER_LEVEL ? ", level" : "");

    return 0;
}

/* the timer's counts in `ns' (divided by 16): from the clock if it's
 * precise, else from the PIT's channel 2 */
static uint32_t timer_calibrate(uint32_t ns)
{
    const uint32_t window_us = 20000;

    lapic_write(LAPIC_TIMER_DIV, TIMER_DIV_16);
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
    lapic_write(LAPIC_TIMER_INIT, 0xffffffff);

    if (x86_clock_precise()) {
        uint64_t t0 = arch_rtime_ns();

        while (arch_rtime_ns() - t0 < window_us * 1000ULL)
            ;
    } else {
        x86_pit_delay(window_us);
    }

    uint32_t counted = 0xffffffff - lapic_read(LAPIC_TIMER_CUR);
    lapic_write(LAPIC_TIMER_INIT, 0);

    return (uint32_t) ((uint64_t) counted * ns / (window_us * 1000ULL));
}

/* the tick, every `period_ns', to `handler'; the period it is */
uint32_t x86_lapic_timer_setup(uint32_t period_ns, x86_irq_handler_t handler)
{
    timer_count = timer_calibrate(period_ns);

    if (!timer_count)
        timer_count = 1;

    x86_irq_handler_install(X86_IRQ_TIMER, handler);

    lapic_write(LAPIC_TIMER_DIV, TIMER_DIV_16);
    lapic_write(LAPIC_LVT_TIMER, X86_TIMER_VECTOR | LVT_PERIODIC);
    lapic_write(LAPIC_TIMER_INIT, timer_count);

    printk("apic: the timer every %d ns (%d counts, divided by 16)\n", period_ns, timer_count);
    return period_ns;
}
