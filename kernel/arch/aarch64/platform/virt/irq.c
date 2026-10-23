/** \file
 * Interrupts' handlers: one an interrupt, installed by its driver; the
 * GIC says which came
 */

#include <core/system.h>
#include <core/string.h>
#include <platform/virt.h>

static void (*handlers[IRQ_NR])(void);
static uint64_t counts[IRQ_NR];

void irq_install(unsigned irq, void (*handler)(void))
{
    if (irq >= IRQ_NR)
        return;

    handlers[irq] = handler;
    gic_enable(irq);
}

/** the interrupt that came: its handler, then the GIC told it's done
 * (before a thread switch: the next ones keep coming) */
void irq_handle(void)
{
    unsigned irq = gic_ack();

    if (irq >= 1020)    /* spurious: none */
        return;

    if (irq < IRQ_NR) {
        counts[irq]++;

        if (handlers[irq])
            handlers[irq]();
    }

    gic_eoi(irq);
}

/** /proc/interrupts */
int arch_interrupts_info(char *buf, size_t size)
{
    int sz = snprintf(buf, size, "controller: gicv2\n");

    for (unsigned i = 0; i < IRQ_NR && (size_t) sz < size; ++i)
        if (counts[i])
            sz += snprintf(buf + sz, size - sz, "%d: %ld\n", i, counts[i]);

    return sz;
}
