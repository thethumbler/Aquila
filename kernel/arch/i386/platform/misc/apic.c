/** \file
 * x86: the APIC, the local APICs and the I/O APICs.
 *
 * When the CPU has an APIC and ACPI's MADT says where its parts are, the
 * APIC brings the interrupts (an `x86_irq_chip`, see irq.c); with
 * `apic=off`, the 8259s do.
 *
 * - Each CPU's local APIC is enabled, with the spurious vector 0xFF. Its
 *   LINT0 input (where the 8259s would come in) is masked, and the NMI
 *   comes on the LINT input the MADT says. Its timer is the scheduler's
 *   tick, periodic, calibrated once against the clock. The MADT's local
 *   APICs are the CPUs; the boot CPU starts the others with IPIs
 *   (cpu/smp.c).
 * - The I/O APICs route the device interrupts. IRQ n goes to the pin that
 *   the MADT's overrides say (for the ISA IRQs: IRQ 0 is pin 2 on most
 *   machines), edge-triggered and active high unless they say otherwise.
 *   QEMU's pc overrides the PCI lines (from the devices' configuration
 *   space) as level-triggered. Every pin goes to the boot CPU with vector
 *   32 + n, and stays masked until a handler is installed.
 * - MSI (`arch_msi_alloc()`) gets an IRQ above the I/O APICs' pins. Its
 *   vector is in the message that the device writes to the local APIC.
 *
 * A PCI device's interrupt line, in its configuration space, is an ISA
 * IRQ: QEMU's chipsets (pc and q35) work that way. A machine whose PCI
 * interrupts only come on the I/O APIC's pins past 15 would need ACPI's
 * `_PRT`; a device with MSI doesn't.
 *
 * References:
 *      [1] - Intel(R) 64 and IA-32 Architectures Software Developer's
 *            Manual, Volume 3, Chapter 11 (APIC)
 *      [2] - 82093AA I/O Advanced Programmable Interrupt Controller
 *            (IOAPIC), Intel
 *      [3] - ACPI Specification, 5.2.12 (MADT)
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/kargs.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <cpu/msr.h>
#include <cpu/sdt.h>
#include <platform/misc.h>
#include <sys/cpu.h>

/* the local APIC's registers */
#define LAPIC_ID        0x020
#define LAPIC_TPR       0x080
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_ICR_LO    0x300
#define LAPIC_ICR_HI    0x310
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

/* the interrupt command register: writing its low half sends the IPI,
 * and this bit says it is still being delivered */
#define ICR_PENDING     0x1000

/* the I/O APIC's registers: one is selected, then read or written
 * through the window */
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

/* the enabled local APICs that the MADT lists, which are the CPUs */
static uint32_t cpu_ids[CPUS_MAX];
static int cpu_ids_nr;

/* the LINT input the NMI comes on, as the MADT says */
static int nmi_lint = 1;

/* the I/O APICs: their registers, and the GSIs of their pins */
static struct ioapic {
    volatile uint32_t *regs;
    uint32_t address, gsi_base, pins;
} ioapics[IOAPICS_MAX];
static int ioapics_nr;

/* the ISA IRQs' GSIs, and their flags (0: the ISA bus's, edge-triggered
 * and active high) */
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

/** Reads a register of the running CPU's local APIC (every CPU sees its
 * own local APIC at the same address). */
static inline uint32_t lapic_read(unsigned reg)
{
    return lapic[reg / 4];
}

/** Writes a register of the running CPU's local APIC. */
static inline void lapic_write(unsigned reg, uint32_t val)
{
    lapic[reg / 4] = val;
}

/** Returns the I/O APIC that has the pin of `gsi`, or NULL if none has. */
static struct ioapic *ioapic_of(uint32_t gsi)
{
    for (int i = 0; i < ioapics_nr; ++i)
        if (gsi >= ioapics[i].gsi_base && gsi < ioapics[i].gsi_base + ioapics[i].pins)
            return &ioapics[i];

    return NULL;
}

/** Reads an I/O APIC register. */
static uint32_t ioapic_read(struct ioapic *io, uint32_t reg)
{
    io->regs[IOAPIC_SEL / 4] = reg;
    return io->regs[IOAPIC_WIN / 4];
}

/** Writes an I/O APIC register. */
static void ioapic_write(struct ioapic *io, uint32_t reg, uint32_t val)
{
    io->regs[IOAPIC_SEL / 4] = reg;
    io->regs[IOAPIC_WIN / 4] = val;
}

/** Returns the GSI (the pin) that IRQ `irq` comes on, and sets how it is
 * triggered. */
static uint32_t irq_gsi(unsigned irq, int *level, int *low)
{
    uint16_t flags = irq < 16 ? isa[irq].flags : 0;
    uint32_t gsi = irq < 16 ? isa[irq].gsi : irq;

    /* the ISA IRQs conform to the bus: edge-triggered, active high; the
     * PCI ones past 16 are level-triggered, active low */
    *level = (flags & ACPI_MPS_TRIGGER_MASK) ? (flags & ACPI_MPS_TRIGGER_MASK) == ACPI_MPS_TRIGGER_LEVEL : irq >= 16;
    *low = (flags & ACPI_MPS_POLARITY_MASK) ? (flags & ACPI_MPS_POLARITY_MASK) == ACPI_MPS_POLARITY_LOW : irq >= 16;

    return gsi;
}

/** Routes IRQ `irq` to the boot CPU, with vector 32 + `irq`, masked or
 * not. */
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

/** Masks an IRQ at the I/O APIC. */
static void apic_mask(unsigned irq)
{
    if (irq < X86_IRQ_TIMER)
        apic_route(irq, 1);
}

/** Unmasks an IRQ at the I/O APIC. */
static void apic_unmask(unsigned irq)
{
    if (irq < X86_IRQ_TIMER)
        apic_route(irq, 0);
}

/** Tells the local APIC that the interrupt is handled. */
static void apic_eoi(unsigned irq)
{
    lapic_write(LAPIC_EOI, 0);
}

/** Tells the running CPU's local APIC that its interrupt (an IPI) is
 * handled. */
void x86_lapic_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

/** Describes an IRQ's route, for /proc/interrupts. */
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

/** Does nothing: an MSI is masked at the device (pci.c), not here; its
 * EOI is the local APIC's. */
static void msi_nothing(unsigned irq)
{
}

/** Describes an MSI's route, for /proc/interrupts. */
static const char *msi_describe(unsigned irq)
{
    return "msi";
}

static struct x86_irq_chip msi_chip = {
    .name     = "msi",
    .mask     = msi_nothing,
    .unmask   = msi_nothing,
    .eoi      = apic_eoi,
    .describe = msi_describe,
};

/**
 * Gets an IRQ for a device's MSI, and installs its handler. The message
 * goes to the boot CPU's local APIC, with the IRQ's vector, edge-triggered.
 *
 * @param handler  the interrupt's handler
 * @param arg      the argument the handler gets
 * @param addr     set to the address the device writes the message to
 * @param data     set to the message
 * @return the IRQ, `-ENODEV` without the APIC (the 8259s take no MSI), or
 *         `-ENOSPC` if there's no free IRQ
 */
int arch_msi_alloc(irq_handler_t handler, void *arg, uint64_t *addr, uint32_t *data)
{
    if (!lapic)
        return -ENODEV;

    int irq = x86_irq_alloc(&msi_chip);

    if (irq < 0)
        return -ENOSPC;

    int err = irq_install(irq, handler, arg);

    if (err)
        return err;

    *addr = 0xFEE00000ULL | (uint64_t) lapic_id << 12;
    *data = 32 + irq;
    return irq;
}

/** Returns whether the CPU has a local APIC (CPUID 1, EDX bit 9). */
static int cpu_has_apic(void)
{
    uint32_t a, b, c, d;

    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return !!(d & (1 << 9));
}

/** Sets up the running CPU's local APIC: enabled, with the spurious vector
 * 0xFF, its timer off, the NMI on the LINT input the MADT says, and the
 * other LINT (the 8259s') masked. */
void x86_lapic_cpu_setup(void)
{
    msr_write(APIC_BASE, msr_read(APIC_BASE) | APIC_BASE_ENABLE);

    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LVT_MASKED);
    lapic_write(nmi_lint ? LAPIC_LVT_LINT1 : LAPIC_LVT_LINT0, LVT_NMI);
    lapic_write(nmi_lint ? LAPIC_LVT_LINT0 : LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_SVR, SVR_ENABLE | X86_SPURIOUS_VECTOR);
}

/**
 * Lists the CPUs' local APIC IDs, the boot CPU's among them.
 *
 * @param ids  where to put them
 * @param max  how many fit
 * @return how many there are, or 0 without the APIC
 */
int x86_apic_cpus(uint32_t *ids, int max)
{
    if (!lapic)
        return 0;

    int n = MIN(cpu_ids_nr, max);

    for (int i = 0; i < n; ++i)
        ids[i] = cpu_ids[i];

    return n;
}

/**
 * Sends an IPI to a CPU, and waits until it is delivered.
 *
 * @param id   the CPU's local APIC ID
 * @param icr  the low half of the interrupt command register: the delivery
 *             mode, the vector, and so on
 */
void x86_lapic_ipi(uint32_t id, uint32_t icr)
{
    lapic_write(LAPIC_ICR_HI, id << 24);
    lapic_write(LAPIC_ICR_LO, icr);

    while (lapic_read(LAPIC_ICR_LO) & ICR_PENDING)
        ;
}

/**
 * Sets up the APIC to bring the interrupts, if the CPU has one, the MADT
 * describes it, and the kernel's command line doesn't say `apic=off`.
 *
 * @return 0 if the APIC brings the interrupts from now on, `-ENODEV` if
 *         there's no APIC to use, or `-ENOMEM` if its registers couldn't
 *         be mapped
 */
int x86_apic_setup(void)
{
    const char *arg = NULL;

    if (!kargs_get("apic", &arg) && arg && !strcmp(arg, "off")) {
        printk("apic: off (apic=off)\n");
        return -ENODEV;
    }

    struct acpi_madt *madt = x86_acpi_table ? x86_acpi_table("APIC") : NULL;

    if (!cpu_has_apic() || !madt) {
        printk("apic: %s\n", madt ? "the CPU has none" : "no MADT");
        return -ENODEV;
    }

    /* the ISA IRQs come on the pins of the same numbers, unless an
     * override says otherwise */
    for (int i = 0; i < 16; ++i) {
        isa[i].gsi = i;
        isa[i].flags = 0;
    }

    uint8_t *p = (uint8_t *) (madt + 1), *end = (uint8_t *) madt + madt->header.length;

    for (; p + 2 <= end && p[1] >= 2; p += p[1]) {
        switch (p[0]) {
            case ACPI_MADT_LOCAL_APIC: {
                struct acpi_madt_local_apic *e = (void *) p;

                if ((e->flags & ACPI_MADT_ENABLED) && cpu_ids_nr < CPUS_MAX)
                    cpu_ids[cpu_ids_nr++] = e->apic_id;
                break;
            }
            case ACPI_MADT_IO_APIC: {
                struct acpi_madt_io_apic *e = (void *) p;

                if (ioapics_nr == IOAPICS_MAX)
                    break;

                struct ioapic *io = &ioapics[ioapics_nr];
                io->regs = arch_mmio_map(e->address, 4096);

                /* no room to map its registers: this one isn't used */
                if (!io->regs)
                    break;

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
        return -ENODEV;
    }

    /* the boot CPU's local APIC, where the MADT says; without room to map
     * it, the 8259s bring the interrupts instead */
    lapic = arch_mmio_map(madt->local_apic_address, 4096);

    if (!lapic) {
        printk("apic: no room to map the local APIC\n");
        return -ENOMEM;
    }

    x86_lapic_cpu_setup();
    lapic_id = lapic_read(LAPIC_ID) >> 24;
    cpus[0].arch.apic_id = lapic_id;

    /* mask every pin until it has a handler */
    for (int i = 0; i < ioapics_nr; ++i)
        for (uint32_t pin = 0; pin < ioapics[i].pins; ++pin)
            ioapic_write(&ioapics[i], IOAPIC_REDIR(pin), REDIR_MASKED);

    /* mask the 8259s, if there are any: they would come through LINT0 */
    x86_pic_disable();

    x86_irq_chip_set(&apic_chip);

    /* MSIs get the IRQs past the I/O APICs' pins */
    uint32_t gsi_end = 16;

    for (int i = 0; i < ioapics_nr; ++i)
        if (ioapics[i].gsi_base + ioapics[i].pins > gsi_end)
            gsi_end = ioapics[i].gsi_base + ioapics[i].pins;

    x86_irq_alloc_from(gsi_end);

    printk("apic: local APIC %d at %p, %d I/O APIC(s), the first at %p (GSI %d, %d pins)\n",
           lapic_id, (void *) (uintptr_t) madt->local_apic_address, ioapics_nr,
           (void *) (uintptr_t) ioapics[0].address, ioapics[0].gsi_base, ioapics[0].pins);

    for (int i = 0; i < 16; ++i)
        if (isa[i].gsi != (uint32_t) i || isa[i].flags)
            printk("apic: IRQ %d is GSI %d%s\n", i, isa[i].gsi,
                   (isa[i].flags & ACPI_MPS_TRIGGER_MASK) == ACPI_MPS_TRIGGER_LEVEL ? ", level" : "");

    return 0;
}

/** Measures how many counts the local APIC's timer makes in `ns`
 * nanoseconds (with its clock divided by 16), against the clock if it is
 * precise, else with the PIT. */
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

/**
 * Starts the scheduler's tick with the local APIC's timer, calibrated
 * first.
 *
 * @param period_ns  the tick's period, in ns
 * @param handler    the tick's handler
 * @return the period
 */
uint32_t x86_lapic_timer_setup(uint32_t period_ns, irq_handler_t handler)
{
    timer_count = timer_calibrate(period_ns);

    if (!timer_count)
        timer_count = 1;

    irq_install(X86_IRQ_TIMER, handler, NULL);

    x86_lapic_timer_cpu_start();

    printk("apic: the timer every %d ns (%d counts, divided by 16)\n", period_ns, timer_count);
    return period_ns;
}

/** Starts the running CPU's local APIC timer, with the boot CPU's
 * calibration, once it has been done. */
void x86_lapic_timer_cpu_start(void)
{
    if (!timer_count)
        return;

    lapic_write(LAPIC_TIMER_DIV, TIMER_DIV_16);
    lapic_write(LAPIC_LVT_TIMER, X86_TIMER_VECTOR | LVT_PERIODIC);
    lapic_write(LAPIC_TIMER_INIT, timer_count);
    curcpu()->arch.ticking = 1;
}
