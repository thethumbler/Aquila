/** \file
 * x86: starting the other CPUs, the application processors (APs).
 *
 * The local APICs that ACPI's MADT lists are the CPUs (`apic.c`). The
 * boot CPU sends each other one an INIT IPI, then a startup IPI (twice if
 * it doesn't answer). The AP starts in real mode at the trampoline's page
 * (`trampoline.S`), which brings it into the kernel, at `x86_ap_start()`,
 * on its own idle stack. There it gets its own GDT, TSS and %gs
 * (`curcpu()`), sets up its local APIC, says that it's there, and idles
 * until an interrupt gives it something to run.
 *
 * The trampoline's page tables map its own page where it is, and the
 * kernel's half as the kernel's tables do (the kernel's entries of the
 * top table never change). The AP switches to the kernel's tables right
 * away, so nothing of the trampoline's stays in a TLB.
 *
 * With `smp=off` on the kernel's command line, only the boot CPU runs.
 *
 * References:
 *      [1] - Intel(R) 64 and IA-32 Architectures Software Developer's
 *            Manual, Volume 3, 9.4 (Multiple-Processor Initialization)
 *            and 11.6 (Issuing Interprocessor Interrupts)
 */

#include <core/system.h>
#include <core/kargs.h>
#include <core/string.h>
#include <core/arch.h>
#include <cpu/cpu.h>
#include <cpu/msr.h>
#include <cpu/smp.h>
#include <mm/mm.h>
#include <mm/pmap.h>
#include <mm/vm.h>
#include <platform/misc.h>
#include <sys/cpu.h>

MALLOC_DEFINE(M_IDLE_STACK, "idle-stack", "a CPU's idle stack");

#define IDLE_STACK_SIZE 8192

/* The IPIs: INIT, and the startup IPI, whose vector is the page where the
 * AP starts. */
#define ICR_INIT        0x00000500
#define ICR_STARTUP     0x00000600
#define ICR_ASSERT      0x00004000

/* trampoline.S */
extern char x86_trampoline[], x86_trampoline_data[], x86_trampoline_end[];

/** What an AP needs to reach the kernel, at `x86_trampoline_data` (the
 * fields are at trampoline.S's `TR_` offsets). */
struct trampoline_data {
    uint64_t cr0, cr3, cr4, efer;
    uint64_t entry;     /* where it goes: x86_ap_start() */
    uint64_t stack;     /* the top of its idle stack */
    uint64_t cpu;       /* its struct cpu */
};

/* the trampoline's page tables, in the pages after its code */
#define TABLES          (X86_TRAMPOLINE + 0x1000)

#define PG_PRESENT      0x001
#define PG_WRITE        0x002
#define PG_LARGE        0x080

#define MSR_EFER        0xC0000080

/* set by the AP that was started, once it is in the kernel */
static volatile int ap_started;

/** Where an AP enters the kernel, from the trampoline: it sets up its own
 * CPU structures, says that it's there, and idles. */
static void __attribute__((noreturn)) x86_ap_start(struct cpu *cpu)
{
    x86_gdt_setup(cpu, VMA(0x100000ULL));
    x86_idt_setup();

    /* switch from the trampoline's page tables to the kernel's */
    pmap_switch(kvm_space.pmap);

    x86_lapic_cpu_setup();

    asm volatile("" ::: "memory");
    ap_started = 1;

    /* idle, with interrupts on, until there's a thread to run */
    cpu->idle = 1;
    arch_idle();
    __builtin_unreachable();
}

/*
 * Makes another CPU enter the kernel now, with the wake IPI: to run a
 * thread that just became ready, or to see that its thread was killed.
 */
void arch_cpu_kick(struct cpu *cpu)
{
    x86_lapic_ipi(cpu->arch.apic_id, X86_WAKE_VECTOR | ICR_ASSERT);
}

/** Builds the trampoline's page tables, which map the trampoline's page
 * where it is and the kernel's half as the kernel's tables do. Returns
 * the top table's physical address. */
static paddr_t tables_build(void)
{
    uintptr_t *top = PHYS2VIRT(TABLES);
    uintptr_t *kernel = PHYS2VIRT(kvm_space.pmap->map);

    memset(PHYS2VIRT(TABLES), 0, 3 * PAGE_SIZE);

#if ARCH_BITS==32
    /* the page directory, and a page table for the first 4 MiB */
    uint32_t *pt = PHYS2VIRT(TABLES + PAGE_SIZE);

    for (int i = 768; i < 1024; ++i)
        top[i] = kernel[i];

    top[0] = (TABLES + PAGE_SIZE) | PG_PRESENT | PG_WRITE;
    pt[X86_TRAMPOLINE >> 12] = X86_TRAMPOLINE | PG_PRESENT | PG_WRITE;
#else
    /* the PML4, a PDPT, and a page directory that maps the first 2 MiB
     * as one large page (not a global one, so it goes away with these
     * tables) */
    uint64_t *pdpt = PHYS2VIRT(TABLES + PAGE_SIZE);
    uint64_t *pd = PHYS2VIRT(TABLES + 2 * PAGE_SIZE);

    for (int i = 256; i < 512; ++i)
        top[i] = kernel[i];

    top[0] = (TABLES + PAGE_SIZE) | PG_PRESENT | PG_WRITE;
    pdpt[0] = (TABLES + 2 * PAGE_SIZE) | PG_PRESENT | PG_WRITE;
    pd[0] = 0 | PG_PRESENT | PG_WRITE | PG_LARGE;
#endif

    return TABLES;
}

/** Starts the CPU whose local APIC is `id`, as `cpu`. Returns 0, or -1 if
 * it didn't answer. */
static int ap_boot(struct cpu *cpu, uint32_t id)
{
    struct trampoline_data *data = PHYS2VIRT(X86_TRAMPOLINE + (x86_trampoline_data - x86_trampoline));

    data->entry = (uintptr_t) x86_ap_start;
    data->stack = cpu->arch.idle_stack;
    data->cpu   = (uintptr_t) cpu;

    ap_started = 0;
    asm volatile("" ::: "memory");

    /* INIT, wait 10 ms, then the startup IPI, wait 200 us, and send it
     * again if the AP isn't there */
    x86_lapic_ipi(id, ICR_INIT | ICR_ASSERT);
    x86_pit_delay(10000);

    for (int sipi = 0; sipi < 2 && !ap_started; ++sipi) {
        x86_lapic_ipi(id, ICR_STARTUP | ICR_ASSERT | (X86_TRAMPOLINE >> 12));
        x86_pit_delay(200);
    }

    /* wait up to 100 ms more */
    for (int ms = 0; ms < 100 && !ap_started; ++ms)
        x86_pit_delay(1000);

    if (ap_started)
        return 0;

    /* Too late: send INIT again, so that the AP waits for a startup IPI
     * that won't come. If it started now, it would run on a stack and a
     * struct cpu that are about to be freed and reused. */
    x86_lapic_ipi(id, ICR_INIT | ICR_ASSERT);
    x86_pit_delay(10000);

    return -1;
}

/**
 * Starts the other CPUs, at boot, unless the kernel's command line says
 * `smp=off`. A CPU that doesn't answer is left out.
 */
void x86_smp_init(void)
{
    const char *arg = NULL;
    uint32_t ids[CPUS_MAX];
    int n = x86_apic_cpus(ids, CPUS_MAX);

    if (n <= 1)
        return;

    if (!kargs_get("smp", &arg) && arg && !strcmp(arg, "off")) {
        printk("smp: off (smp=off), %d CPUs\n", n);
        return;
    }

    /* copy the trampoline to its page */
    size_t size = x86_trampoline_end - x86_trampoline;

    if (size > PAGE_SIZE) {
        printk("smp: the trampoline is more than a page\n");
        return;
    }

    memcpy(PHYS2VIRT(X86_TRAMPOLINE), x86_trampoline, size);

    struct trampoline_data *data = PHYS2VIRT(X86_TRAMPOLINE + (x86_trampoline_data - x86_trampoline));

    /* the APs take the boot CPU's control registers (the FPU's bits,
     * write protection, long mode), and the trampoline's page tables */
    data->cr0 = read_cr0();
    data->cr3 = tables_build();
    data->cr4 = read_cr4();
#if ARCH_BITS==64
    data->efer = msr_read(MSR_EFER);
#else
    data->efer = 0;
#endif

    for (int i = 0; i < n && cpus_count < CPUS_MAX; ++i) {
        if (ids[i] == cpus[0].arch.apic_id)
            continue;

        struct cpu *cpu = &cpus[cpus_count];
        void *stack = kmalloc(IDLE_STACK_SIZE, &M_IDLE_STACK, 0);

        if (!stack)
            break;

        memset(cpu, 0, sizeof(*cpu));
        cpu->id = cpus_count;
        cpu->arch.apic_id = ids[i];
        cpu->arch.idle_stack = (uintptr_t) stack + IDLE_STACK_SIZE;

        if (ap_boot(cpu, ids[i])) {
            printk("smp: the CPU with local APIC %d didn't start\n", ids[i]);
            kfree(stack);
            continue;
        }

        cpus_count++;
    }

    printk("smp: %d CPUs\n", cpus_count);
}
