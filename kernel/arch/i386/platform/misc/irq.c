/** \file
 * x86: devices' interrupts (IRQs), whichever controller brings them: the
 * 8259s or the APIC, each an `x86_irq_chip`.
 *
 * IRQ n is vector 32 + n. With the 8259s, the IRQs are the ISA bus's 16.
 * With the APIC, they are what the I/O APICs route from their pins: the
 * ISA IRQs too (on the pins the MADT says), and PCI devices' lines (from
 * their configuration space). The local APIC's timer, vector 240, is an
 * IRQ of its own here (`X86_IRQ_TIMER`).
 *
 * An IRQ may have several handlers, because a PCI line can be shared:
 * each is called, and each looks at its own device. An IRQ may also have
 * a controller of its own: an MSI's is the device itself
 * (`x86_irq_alloc()` gives those IRQs, above the I/O APICs' pins).
 *
 * After the handlers, the controller is told that the interrupt is done
 * (the EOI; a level-triggered line would raise it again before). Only
 * then may what the interrupt woke up run (an urgent thread, or the
 * thread switch the tick asked for).
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

/** A handler installed for an IRQ. */
struct handler {
    irq_handler_t fn;
    void *arg;
    struct handler *next;
};

/* each IRQ's handlers, how many times it came, and its own controller */
static struct {
    struct handler *handlers;
    uint64_t count;
    struct x86_irq_chip *chip;      /* its own controller (MSI), else NULL */
} irqs[X86_IRQ_NR];

/* the system's interrupt controller: the 8259s or the APIC */
static struct x86_irq_chip *chip;

/* the first IRQ x86_irq_alloc() may give; none until the APIC sets it */
static unsigned alloc_first = X86_IRQ_TIMER;

/** Returns the controller of an IRQ: its own, or the system's. */
static struct x86_irq_chip *chip_of(unsigned irq)
{
    return irqs[irq].chip ? irqs[irq].chip : chip;
}

/**
 * Sets the system's interrupt controller.
 *
 * @param c  the controller: the 8259s or the APIC
 */
void x86_irq_chip_set(struct x86_irq_chip *c)
{
    chip = c;
}

/**
 * Returns the name of the system's interrupt controller.
 *
 * @return its name, or "none"
 */
const char *x86_irq_chip_name(void)
{
    return chip ? chip->name : "none";
}

/**
 * Sets the first IRQ that `x86_irq_alloc()` may give: past the I/O APICs'
 * last pin.
 *
 * @param first  the first IRQ
 */
void x86_irq_alloc_from(unsigned first)
{
    alloc_first = first;
}

/**
 * Gives a free IRQ with a controller of its own, for an MSI.
 *
 * @param c  the IRQ's controller
 * @return the IRQ, or -1 if there's no free one
 */
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

/*
 * Installs a handler for an IRQ. The first handler unmasks the IRQ at its
 * controller.
 */
int irq_install(unsigned irq, irq_handler_t fn, void *arg)
{
    if (irq >= X86_IRQ_NR)
        return -EINVAL;

    struct handler *h = kmalloc(sizeof(*h), &M_IRQ, M_ZERO);

    if (!h)
        return -ENOMEM;

    h->fn  = fn;
    h->arg = arg;

    /* the first handler: the controller lets the IRQ through */
    int first = !irqs[irq].handlers;
    struct handler **p = &irqs[irq].handlers;

    while (*p)
        p = &(*p)->next;

    *p = h;

    if (first && chip_of(irq) && irq != X86_IRQ_TIMER)
        chip_of(irq)->unmask(irq);

    return 0;
}

/*
 * Removes a handler of an IRQ. The last one gone masks the IRQ at its
 * controller.
 */
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

    /* the last handler is gone: the controller keeps the IRQ out */
    if (!irqs[irq].handlers && chip_of(irq) && irq != X86_IRQ_TIMER)
        chip_of(irq)->mask(irq);
}

/** Asks for a thread switch once the current interrupt is done: the tick
 * calls it. */
void x86_irq_tick_switch(void)
{
    curcpu()->arch.tick_switch = 1;
}

/** Runs an interrupt's handlers, then tells its controller that it is
 * done. */
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

/** Handles an interrupt, then switches threads if the tick asked for it
 * or an urgent thread was woken, and returns to what was interrupted. */
static void irq_dispatch(struct x86_regs *r)
{
    irq_handle(r);

    /* the tick asked for a switch: the running thread goes back to the
     * ready list, and the next one runs (if the CPU was idle, it runs) */
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

    /* an urgent thread (the network's) was woken: it runs now, not at the
     * next tick, and the interrupted thread is ready again */
    if (sched_urgent && (r->cs & 3) == 3 && curthread) {
        extern void x86_preempt(void);
        x86_preempt();
    }

    /* return to user mode (maybe after other threads ran), delivering the
     * signals that came meanwhile */
    x86_return_to_user(r, 0);
}

/**
 * Handles an interrupt: a device's, the tick, or an IPI. The stubs in
 * cpu.S call it. It takes the kernel lock if it came from user mode or the
 * idle loop (in the kernel, the lock is held already), and lets go of it
 * on the way back. The TLB IPI is handled without the lock, because the
 * CPU that sent it holds it, waiting.
 *
 * @param r  the interrupted registers, saved on the kernel stack
 */
void __x86_irq_handler(struct x86_regs *r)
{
    if (r->vec == X86_TLB_VECTOR) {
        x86_tlb_ipi();
        x86_lapic_eoi();
        return;
    }

    int took = kernel_enter();

    /* a CPU woken up for the first time starts its tick, now that the
     * scheduler runs */
    if (r->vec == X86_WAKE_VECTOR && !curcpu()->arch.ticking)
        x86_lapic_timer_cpu_start();

    /* its process was killed while it ran (from another CPU): handle the
     * interrupt (the device's, and the controller's EOI), then the thread
     * ends */
    if (took && curthread && curthread->killed) {
        irq_handle(r);
        sched_thread_killed();
    }

    irq_dispatch(r);
    kernel_leave(took);
}

/** Sets the IDT's gates for the interrupts: vectors 32 to 79, the local
 * APIC's timer, the TLB and wake IPIs, and its spurious interrupt. */
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

/**
 * Writes /proc/interrupts: the controller, then how many times each IRQ
 * with handlers came, and how many TLB flushes each CPU did for another.
 *
 * @param buf   where to write it
 * @param size  the buffer's size
 * @return the length of the text
 */
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

    /* how many TLB flushes each CPU did for another (mm/tlb.c) */
    if (cpus_count > 1 && (size_t) n < size) {
        n += snprintf(buf + n, size - n, "tlb:");

        for (int i = 0; i < cpus_count && (size_t) n < size; ++i)
            n += snprintf(buf + n, size - n, " %ld", cpus[i].arch.tlb_flushes);

        if ((size_t) n < size)
            n += snprintf(buf + n, size - n, "\n");
    }

    return n;
}
