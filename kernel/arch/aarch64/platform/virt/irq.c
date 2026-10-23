/** \file
 * Interrupts' handlers, installed by their drivers (dev/irq.h): an
 * interrupt may have several (a shared line), each is called. The GIC
 * says which came; the first handler enables it, the last one gone
 * disables it.
 */

#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <dev/irq.h>
#include <platform/virt.h>

MALLOC_DEFINE(M_IRQ, "irq", "IRQ handler");

struct handler {
    irq_handler_t fn;
    void *arg;
    struct handler *next;
};

static struct handler *handlers[IRQ_NR];
static uint64_t counts[IRQ_NR];

int irq_install(unsigned irq, irq_handler_t fn, void *arg)
{
    if (irq >= IRQ_NR)
        return -1;

    struct handler *h = kmalloc(sizeof(*h), &M_IRQ, M_ZERO);

    if (!h)
        return -1;

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

/** the interrupt that came: its handlers, then the GIC told it's done
 * (before a thread switch: the next ones keep coming) */
void irq_handle(void)
{
    unsigned irq = gic_ack();

    if (irq >= 1020)    /* spurious: none */
        return;

    if (irq < IRQ_NR) {
        counts[irq]++;

        for (struct handler *h = handlers[irq]; h; h = h->next)
            h->fn(h->arg);
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
