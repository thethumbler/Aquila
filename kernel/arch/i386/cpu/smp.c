/*
 *          SMP: the other CPUs, the application processors (APs)
 *
 * References:
 *      [1] - Intel(R) 64 and IA-32 Architectures Software Developer's
 *            Manual, Volume 3, 9.4 (Multiple-Processor Initialization)
 *            and 11.6 (Issuing Interprocessor Interrupts)
 *
 * The MADT's local APICs are the CPUs (apic.c). Each other one is sent an
 * INIT IPI, then a startup IPI (twice if it doesn't answer): it starts in
 * real mode at the trampoline's page (trampoline.S), which brings it to the
 * kernel, x86_ap_start(), on its own idle stack. It has its own GDT, TSS
 * and %gs (curcpu()), and its local APIC; it says it's there, and idles
 * (the TLB's IPIs come).
 *
 * The trampoline's page tables map its page where it is, and the kernel's
 * half as the kernel's tables do (the kernel's entries of the top table
 * never change): the AP leaves them for the kernel's, nothing of them
 * stays in the kernel's tables or in a TLB.
 *
 * smp=off on the command line: the boot CPU only.
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

/* the IPIs: INIT, and the startup IPI (its vector the page it starts at) */
#define ICR_INIT        0x00000500
#define ICR_STARTUP     0x00000600
#define ICR_ASSERT      0x00004000

/* trampoline.S */
extern char x86_trampoline[], x86_trampoline_data[], x86_trampoline_end[];

/* at x86_trampoline_data: what an AP takes (trampoline.S's TR_ offsets) */
struct trampoline_data {
    uint64_t cr0, cr3, cr4, efer;
    uint64_t entry;     /* x86_ap_start */
    uint64_t stack;     /* its idle stack's top */
    uint64_t cpu;       /* its struct cpu */
};

/* the trampoline's page tables, after its code */
#define TABLES          (X86_TRAMPOLINE + 0x1000)

#define PG_PRESENT      0x001
#define PG_WRITE        0x002
#define PG_LARGE        0x080

#define MSR_EFER        0xC0000080

/* the AP that was started is there */
static volatile int ap_started;

static void __attribute__((noreturn)) x86_ap_start(struct cpu *cpu)
{
    x86_gdt_setup(cpu, VMA(0x100000ULL));
    x86_idt_setup();

    /* the kernel's tables, the trampoline's are left */
    pmap_switch(kvm_space.pmap);

    x86_lapic_cpu_setup();

    asm volatile("" ::: "memory");
    ap_started = 1;

    /* idle: interrupts on, the IPIs come */
    cpu->idle = 1;
    arch_idle();
    __builtin_unreachable();
}

/* `cpu' enters the kernel: the wake IPI */
void arch_cpu_kick(struct cpu *cpu)
{
    x86_lapic_ipi(cpu->arch.apic_id, X86_WAKE_VECTOR | ICR_ASSERT);
}

/* the trampoline's page tables: its page, where it is, and the kernel's
 * half; the top table's address */
static paddr_t tables_build(void)
{
    uintptr_t *top = PHYS2VIRT(TABLES);
    uintptr_t *kernel = PHYS2VIRT(kvm_space.pmap->map);

    memset(PHYS2VIRT(TABLES), 0, 3 * PAGE_SIZE);

#if ARCH_BITS==32
    /* the page directory, its table for the first 4 MiB */
    uint32_t *pt = PHYS2VIRT(TABLES + PAGE_SIZE);

    for (int i = 768; i < 1024; ++i)
        top[i] = kernel[i];

    top[0] = (TABLES + PAGE_SIZE) | PG_PRESENT | PG_WRITE;
    pt[X86_TRAMPOLINE >> 12] = X86_TRAMPOLINE | PG_PRESENT | PG_WRITE;
#else
    /* the PML4, a PDPT and a PD for the first 2 MiB, a large page (not a
     * global one: it goes with these tables) */
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

/* the CPU with the local APIC `id' started, as `cpu': 0, -1 if it didn't
 * come */
static int ap_boot(struct cpu *cpu, uint32_t id)
{
    struct trampoline_data *data = PHYS2VIRT(X86_TRAMPOLINE + (x86_trampoline_data - x86_trampoline));

    data->entry = (uintptr_t) x86_ap_start;
    data->stack = cpu->arch.idle_stack;
    data->cpu   = (uintptr_t) cpu;

    ap_started = 0;
    asm volatile("" ::: "memory");

    /* INIT, 10 ms, the startup IPI, 200 us, and again if it isn't there */
    x86_lapic_ipi(id, ICR_INIT | ICR_ASSERT);
    x86_pit_delay(10000);

    for (int sipi = 0; sipi < 2 && !ap_started; ++sipi) {
        x86_lapic_ipi(id, ICR_STARTUP | ICR_ASSERT | (X86_TRAMPOLINE >> 12));
        x86_pit_delay(200);
    }

    /* up to 100 ms more */
    for (int ms = 0; ms < 100 && !ap_started; ++ms)
        x86_pit_delay(1000);

    return ap_started ? 0 : -1;
}

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

    /* the trampoline, at its page */
    size_t size = x86_trampoline_end - x86_trampoline;

    if (size > PAGE_SIZE) {
        printk("smp: the trampoline is more than a page\n");
        return;
    }

    memcpy(PHYS2VIRT(X86_TRAMPOLINE), x86_trampoline, size);

    struct trampoline_data *data = PHYS2VIRT(X86_TRAMPOLINE + (x86_trampoline_data - x86_trampoline));

    /* the boot CPU's control registers (the FPU's bits, write protection,
     * long mode), the trampoline's tables */
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
