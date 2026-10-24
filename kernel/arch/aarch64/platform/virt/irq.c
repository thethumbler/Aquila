/** \file
 * The interrupts' handlers on the virt machine, installed by their
 * drivers (see `dev/irq.h`). An interrupt may have several handlers (a
 * shared line), and each is called. The GIC says which interrupt came;
 * the first handler installed enables it, and the last one uninstalled
 * disables it.
 */

#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <dev/irq.h>
#include <platform/virt.h>
#include <aquila/errno.h>

MALLOC_DEFINE(M_IRQ, "irq", "IRQ handler");

/** An installed handler, on its interrupt's list. */
struct handler {
    irq_handler_t fn;
    void *arg;
    struct handler *next;
};

/* Each interrupt's handlers, and how many times it came. */
static struct handler *handlers[IRQ_NR];
static uint64_t counts[IRQ_NR];

/*
 * Installs a handler for an interrupt.
 */
int irq_install(unsigned irq, irq_handler_t fn, void *arg)
{
    if (irq >= IRQ_NR)
        return -EINVAL;

    struct handler *h = kmalloc(sizeof(*h), &M_IRQ, M_ZERO);

    if (!h)
        return -ENOMEM;

    h->fn  = fn;
    h->arg = arg;

    int first = !handlers[irq];
    struct handler **p = &handlers[irq];

    while (*p)
        p = &(*p)->next;

    *p = h;

    if (first)
        gic_enable(irq);

    return 0;
}

/*
 * Uninstalls a handler that irq_install() installed.
 */
void irq_uninstall(unsigned irq, irq_handler_t fn, void *arg)
{
    if (irq >= IRQ_NR)
        return;

    for (struct handler **p = &handlers[irq]; *p; p = &(*p)->next) {
        struct handler *h = *p;

        if (h->fn == fn && h->arg == arg) {
            *p = h->next;
            kfree(h);
            break;
        }
    }

    if (!handlers[irq])
        gic_disable(irq);
}

/** Runs the handlers of the interrupt that came, then tells the GIC it's
 * done, before any thread switch, so that the next interrupts keep
 * coming. */
void irq_handle(void)
{
    unsigned irq = gic_ack();

    if (irq >= 1020)    /* a spurious interrupt: there's none */
        return;

    if (irq < IRQ_NR) {
        counts[irq]++;

        for (struct handler *h = handlers[irq]; h; h = h->next)
            h->fn(h->arg);
    }

    gic_eoi(irq);
}

/*
 * Writes `/proc/interrupts`: the controller, and how many times each
 * interrupt came.
 */
int arch_interrupts_info(char *buf, size_t size)
{
    int sz = snprintf(buf, size, "controller: gicv2\n");

    for (unsigned i = 0; i < IRQ_NR && (size_t) sz < size; ++i)
        if (counts[i])
            sz += snprintf(buf + sz, size - sz, "%d: %ld\n", i, counts[i]);

    return sz;
}
