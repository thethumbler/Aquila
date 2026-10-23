/** \file
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
#include <dev/irq.h>
#include <platform/misc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <sys/lock.h>
#include <mm/pmap.h>

MALLOC_DEFINE(M_IRQ, "irq", "IRQ handler");

struct handler {
    irq_handler_t fn;
    void *arg;
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

void x86_irq_chip_set(struct x86_irq_chip *c)
{
    chip = c;
}

const char *x86_irq_chip_name(void)
{
    return chip ? chip->name : "none";
}

/** IRQs from `first' on may be allocated (the I/O APIC's pins end there) */
void x86_irq_alloc_from(unsigned first)
{
    alloc_first = first;
}

/** a free IRQ with a controller of its own (MSI), -1 if there's none */
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

int irq_install(unsigned irq, irq_handler_t fn, void *arg)
{
    if (irq >= X86_IRQ_NR)
        return -1;

    struct handler *h = kmalloc(sizeof(*h), &M_IRQ, M_ZERO);

    if (!h)
        return -1;

    h->fn  = fn;
    h->arg = arg;

    /* the first: the controller lets it through */
    int first = !irqs[irq].handlers;
    struct handler **p = &irqs[irq].handlers;

    while (*p)
        p = &(*p)->next;

    *p = h;

    if (first && chip_of(irq) && irq != X86_IRQ_TIMER)
        chip_of(irq)->unmask(irq);

    return 0;
}

void irq_uninstall(unsigned irq, irq_handler_t fn, void *arg)
{
    if (irq >= X86_IRQ_NR)
        return;

    for (struct handler **p = &irqs[irq].handlers; *p; p = &(*p)->next) {
        struct handler *h = *p;

        if (h->fn == fn && h->arg == arg) {
            *p = h->next;
            kfree(h);
            break;
        }
    }

    /* the last: the controller keeps it out */
    if (!irqs[irq].handlers && chip_of(irq) && irq != X86_IRQ_TIMER)
        chip_of(irq)->mask(irq);
}

/** the tick: switch to the next thread once the interrupt is done */
void x86_irq_tick_switch(void)
{
    curcpu()->arch.tick_switch = 1;
}

/** the IRQ's handlers, and the controller done with it */
static void irq_handle(struct x86_regs *r)
{
    uint32_t vec = r->vec;
    unsigned irq = vec == X86_TIMER_VECTOR ? X86_IRQ_TIMER : vec - 32;

    if (irq < X86_IRQ_NR) {
        irqs[irq].count++;
        curcpu()->arch.irq_regs = r;

        for (struct handler *h = irqs[irq].handlers; h; h = h->next)
            h->fn(h->arg);
    }

    struct x86_irq_chip *c = irq < X86_IRQ_NR ? chip_of(irq) : chip;

    if (c)
        c->eoi(irq);
}

static void irq_dispatch(struct x86_regs *r)
{
    irq_handle(r);

    /* the tick: the running thread back to the queue, the next one runs
     * (the CPU was idle: it runs) */
    if (curcpu()->arch.tick_switch) {
        curcpu()->arch.tick_switch = 0;

        if (curcpu()->idle) {
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

    /* back to user mode (maybe after other threads ran): the signals
     * that came meanwhile */
    x86_return_to_user(r, 0);
}

/**
 * An interrupt: with the kernel lock, which it takes if it came from user
 * mode or the idle loop (an exception in the kernel has it already), and
 * lets go of before going back there. The TLB's IPI is done without it:
 * the CPU that sent it has it, waiting.
 */
void __x86_irq_handler(struct x86_regs *r)
{
    if (r->vec == X86_TLB_VECTOR) {
        x86_tlb_ipi();
        x86_lapic_eoi();
        return;
    }

    int took = kernel_enter();

    /* woken: its tick runs, now that the scheduler does */
    if (r->vec == X86_WAKE_VECTOR && !curcpu()->arch.ticking)
        x86_lapic_timer_cpu_start();

    /* its process was killed while it ran here (on another CPU): the
     * interrupt handled (a device's, the controller's EOI), it goes */
    if (took && curthread && curthread->killed) {
        irq_handle(r);
        sched_thread_killed();
    }

    irq_dispatch(r);
    kernel_leave(took);
}

/** the vectors: 32 to 32 + 47, the local APIC's timer, the TLB's and the
 * wake IPIs, and its spurious interrupt */
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
        __x86_irq240(void), __x86_irq241(void), __x86_irq242(void), __x86_spurious(void);

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
    x86_idt_gate_set(X86_TLB_VECTOR, (uintptr_t) __x86_irq241);
    x86_idt_gate_set(X86_WAKE_VECTOR, (uintptr_t) __x86_irq242);
    x86_idt_gate_set(X86_SPURIOUS_VECTOR, (uintptr_t) __x86_spurious);
}

/** /proc/interrupts: the controller, then each IRQ with handlers */
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

    /* the TLB flushes the CPUs did for another (mm/tlb.c), each CPU's */
    if (cpus_count > 1 && (size_t) n < size) {
        n += snprintf(buf + n, size - n, "tlb:");

        for (int i = 0; i < cpus_count && (size_t) n < size; ++i)
            n += snprintf(buf + n, size - n, " %ld", cpus[i].arch.tlb_flushes);

        if ((size_t) n < size)
            n += snprintf(buf + n, size - n, "\n");
    }

    return n;
}
