/*
 * IRQs: devices' interrupts, whichever controller brings them (the 8259,
 * or the APIC: an x86_irq_chip)
 *
 * IRQ n is vector 32 + n: the 8259's 16 (ISA's numbers), or what the I/O
 * APIC routes from its pins (ISA's numbers too, from the pins the MADT
 * says, PCI's from the line in its configuration space). The local APIC's
 * timer is vector 240, an IRQ of its own here (X86_IRQ_TIMER).
 *
 * An IRQ may have several handlers (a PCI line is shared): each is
 * called, each looks at its device. An IRQ may have a controller of its
 * own: an MSI's is the device's (x86_irq_alloc gives them, above the I/O
 * APIC's pins). Then the controller is told it's
 * done (EOI: a level-triggered line would come again before); then what
 * the interrupt woke up may run (an urgent thread, the scheduler's tick):
 * a thread switch, once the controller is done with the interrupt.
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <platform/misc.h>
#include <sys/sched.h>
#include <sys/signal.h>

MALLOC_DEFINE(M_IRQ, "irq", "IRQ handler");

struct handler {
    x86_irq_handler_t fn;
    struct handler *next;
};

static struct {
    struct handler *handlers;
    uint64_t count;
    struct x86_irq_chip *chip;      /* its own (MSI), else the system's */
} irqs[X86_IRQ_NR];

static struct x86_irq_chip *chip;

/* the first IRQ x86_irq_alloc() may give: none until the APIC says */
static unsigned alloc_first = X86_IRQ_TIMER;

static struct x86_irq_chip *chip_of(unsigned irq)
{
    return irqs[irq].chip ? irqs[irq].chip : chip;
}

/* the scheduler's tick asked for a switch (x86_irq_tick_switch) */
static int tick_switch;

void x86_irq_chip_set(struct x86_irq_chip *c)
{
    chip = c;
}

const char *x86_irq_chip_name(void)
{
    return chip ? chip->name : "none";
}

/* IRQs from `first' on may be allocated (the I/O APIC's pins end there) */
void x86_irq_alloc_from(unsigned first)
{
    alloc_first = first;
}

/* a free IRQ with a controller of its own (MSI), -1 if there's none */
int x86_irq_alloc(struct x86_irq_chip *c)
{
    for (unsigned irq = alloc_first; irq < X86_IRQ_TIMER; ++irq) {
        if (!irqs[irq].handlers && !irqs[irq].chip) {
            irqs[irq].chip = c;
            return irq;
        }
    }

    return -1;
}

void x86_irq_handler_install(unsigned irq, x86_irq_handler_t fn)
{
    if (irq >= X86_IRQ_NR)
        panic("irq: no such IRQ");

    struct handler *h = kmalloc(sizeof(*h), &M_IRQ, M_ZERO);

    if (!h)
        panic("irq: no memory for a handler");

    h->fn = fn;

    /* the first: the controller lets it through */
    int first = !irqs[irq].handlers;
    struct handler **p = &irqs[irq].handlers;

    while (*p)
        p = &(*p)->next;

    *p = h;

    if (first && chip_of(irq) && irq != X86_IRQ_TIMER)
        chip_of(irq)->unmask(irq);
}

void x86_irq_handler_uninstall(unsigned irq)
{
    if (irq >= X86_IRQ_NR)
        return;

    if (chip_of(irq) && irq != X86_IRQ_TIMER)
        chip_of(irq)->mask(irq);

    while (irqs[irq].handlers) {
        struct handler *h = irqs[irq].handlers;
        irqs[irq].handlers = h->next;
        kfree(h);
    }
}

/* the tick: switch to the next thread once the interrupt is done */
void x86_irq_tick_switch(void)
{
    tick_switch = 1;
}

void __x86_irq_handler(struct x86_regs *r)
{
    extern uint32_t __x86_isr_int_num;
    uint32_t vec = __x86_isr_int_num;
    unsigned irq = vec == X86_TIMER_VECTOR ? X86_IRQ_TIMER : vec - 32;

    if (irq < X86_IRQ_NR) {
        irqs[irq].count++;

        for (struct handler *h = irqs[irq].handlers; h; h = h->next)
            h->fn(r);
    }

    /* done with it */
    struct x86_irq_chip *c = irq < X86_IRQ_NR ? chip_of(irq) : chip;

    if (c)
        c->eoi(irq);

    /* the tick: the running thread back to the queue, the next one runs
     * (the CPU was idle: it runs) */
    if (tick_switch) {
        tick_switch = 0;

        if (kidle) {
            schedule();
        } else {
            extern void x86_preempt(void);
            x86_preempt();
        }
    }

    sched_idle_wakeup();

    /* an urgent thread was woken (the network's): it runs now, not at the
     * next tick; the interrupted thread is ready again */
    if (sched_urgent && (r->cs & 3) == 3 && curthread) {
        extern void x86_preempt(void);
        x86_preempt();
    }

    /* back to user mode (maybe after other threads ran): signals queued
     * meanwhile (by drivers) are delivered, a stop that was left for the
     * thread is taken now */
    if ((r->cs & 3) == 3 && curthread && curthread->arch) {
        int sig = curthread->sig_defer ? 0 : signal_next(curproc);

        if (sig)
            arch_handle_signal(sig);

        signal_stop_pending();
    }
}

/* the vectors: 32 to 32 + 47, the local APIC's timer and spurious */
void x86_irq_gates_setup(void)
{
    extern void __x86_irq0(void), __x86_irq1(void), __x86_irq2(void), __x86_irq3(void),
        __x86_irq4(void), __x86_irq5(void), __x86_irq6(void), __x86_irq7(void),
        __x86_irq8(void), __x86_irq9(void), __x86_irq10(void), __x86_irq11(void),
        __x86_irq12(void), __x86_irq13(void), __x86_irq14(void), __x86_irq15(void),
        __x86_irq16(void), __x86_irq17(void), __x86_irq18(void), __x86_irq19(void),
        __x86_irq20(void), __x86_irq21(void), __x86_irq22(void), __x86_irq23(void),
        __x86_irq24(void), __x86_irq25(void), __x86_irq26(void), __x86_irq27(void),
        __x86_irq28(void), __x86_irq29(void), __x86_irq30(void), __x86_irq31(void),
        __x86_irq32(void), __x86_irq33(void), __x86_irq34(void), __x86_irq35(void),
        __x86_irq36(void), __x86_irq37(void), __x86_irq38(void), __x86_irq39(void),
        __x86_irq40(void), __x86_irq41(void), __x86_irq42(void), __x86_irq43(void),
        __x86_irq44(void), __x86_irq45(void), __x86_irq46(void), __x86_irq47(void),
        __x86_irq240(void), __x86_spurious(void);

    static void (*stubs[48])(void) = {
        __x86_irq0, __x86_irq1, __x86_irq2, __x86_irq3, __x86_irq4, __x86_irq5,
        __x86_irq6, __x86_irq7, __x86_irq8, __x86_irq9, __x86_irq10, __x86_irq11,
        __x86_irq12, __x86_irq13, __x86_irq14, __x86_irq15, __x86_irq16, __x86_irq17,
        __x86_irq18, __x86_irq19, __x86_irq20, __x86_irq21, __x86_irq22, __x86_irq23,
        __x86_irq24, __x86_irq25, __x86_irq26, __x86_irq27, __x86_irq28, __x86_irq29,
        __x86_irq30, __x86_irq31, __x86_irq32, __x86_irq33, __x86_irq34, __x86_irq35,
        __x86_irq36, __x86_irq37, __x86_irq38, __x86_irq39, __x86_irq40, __x86_irq41,
        __x86_irq42, __x86_irq43, __x86_irq44, __x86_irq45, __x86_irq46, __x86_irq47,
    };

    for (int i = 0; i < 48; ++i)
        x86_idt_gate_set(32 + i, (uintptr_t) stubs[i]);

    x86_idt_gate_set(X86_TIMER_VECTOR, (uintptr_t) __x86_irq240);
    x86_idt_gate_set(X86_SPURIOUS_VECTOR, (uintptr_t) __x86_spurious);
}

/* /proc/interrupts: the controller, then each IRQ with handlers */
int arch_interrupts_info(char *buf, size_t size)
{
    int n = snprintf(buf, size, "controller: %s\n", x86_irq_chip_name());

    for (unsigned irq = 0; irq < X86_IRQ_NR && (size_t) n < size; ++irq) {
        if (!irqs[irq].handlers && !irqs[irq].count)
            continue;

        char line[96];

        if (irq == X86_IRQ_TIMER)
            snprintf(line, sizeof(line), "timer: %ld %s\n", irqs[irq].count,
                     chip && chip->timer ? chip->timer : "");
        else
            snprintf(line, sizeof(line), "%d: %ld %s\n", irq, irqs[irq].count,
                     chip_of(irq) && chip_of(irq)->describe ? chip_of(irq)->describe(irq) : "");

        n += snprintf(buf + n, size - n, "%s", line);
    }

    return n;
}
