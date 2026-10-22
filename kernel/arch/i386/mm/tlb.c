/*
 *          The TLBs: each CPU's, flushed when a mapping changes
 *
 * A CPU caches the mappings it uses in its TLB. When the tables change
 * (a page unmapped, made read-only, another page in its place), the CPU
 * that changed them flushes its own TLB, and asks the others that may
 * have the old mapping to flush theirs, with an IPI, and waits until
 * they have: the page can't be reused while one may still write to it.
 *
 * The ones that may have it: for a process's tables, the CPUs they're
 * loaded on (pmap->cpus, which pmap_switch() keeps). Only the CPU with
 * the kernel lock changes tables, one request at a time then. The others
 * do it without the lock (it's taken), from the IPI's handler or, if they
 * wait for the lock with interrupts off, from the wait (arch_lock_spin()).
 *
 * The kernel's mappings (shared by all tables) are every CPU's, but they
 * aren't asked: outside the lock, a CPU only touches memory that was
 * mapped before it last took the lock and stays mapped while it uses it
 * (its idle stack, its thread's kernel stack, struct cpu, the lock, the
 * tables it has loaded). It flushes its TLB when it takes the lock, if
 * the kernel's mappings changed since it last did (kernel_gen).
 *
 * The kernel's pages aren't global but in the direct map, which doesn't
 * change: reloading CR3 flushes all the others.
 */

#include <core/system.h>
#include <core/arch.h>
#include <cpu/cpu.h>
#include <mm/mm.h>
#include <mm/pmap.h>
#include <mm/vm.h>
#include <platform/misc.h>
#include <sys/cpu.h>
#include <sys/lock.h>

/* more pages than that: the whole TLB */
#define FLUSH_PAGES_MAX     32

#define ICR_ASSERT          0x4000

/* the kernel's mappings changed, how many times (each CPU's TLB is as of
 * one of them, cpu->arch.kernel_gen) */
static uint64_t kernel_gen;

static struct {
    struct pmap *pmap;
    vaddr_t sva, eva;
    int leave;
    volatile uint32_t pending;  /* the CPUs that haven't done it yet */
} request;

static void flush(vaddr_t sva, vaddr_t eva)
{
    if ((eva - sva) / PAGE_SIZE > FLUSH_PAGES_MAX) {
        tlb_flush();
        return;
    }

    for (vaddr_t va = sva; va < eva; va += PAGE_SIZE)
        asm volatile("invlpg (%0)" :: "r"(va) : "memory");
}

/* what the request asks of the running CPU */
static void request_do(struct pmap *pmap, vaddr_t sva, vaddr_t eva, int leave)
{
    struct pmap *loaded = curcpu()->arch.pmap;

    if (leave) {
        if (loaded == pmap)
            pmap_switch(kvm_space.pmap);
    } else if (pmap == kvm_space.pmap || loaded == pmap) {
        flush(sva, eva);
    }
}

/* the IPI's handler, and the lock's wait: the request, if it's for us */
void x86_tlb_ipi(void)
{
    uint32_t me = 1U << curcpu()->id;

    if (!(__atomic_load_n(&request.pending, __ATOMIC_ACQUIRE) & me))
        return;

    request_do(request.pmap, request.sva, request.eva, request.leave);
    curcpu()->arch.tlb_flushes++;
    __atomic_and_fetch(&request.pending, ~me, __ATOMIC_RELEASE);
}

void arch_lock_spin(void)
{
    x86_tlb_ipi();
    asm volatile("pause");
}

/* the lock taken: the kernel's mappings as they are now */
void arch_lock_taken(void)
{
    struct cpu *cpu = curcpu();

    if (cpu->arch.kernel_gen != kernel_gen) {
        tlb_flush();
        cpu->arch.kernel_gen = kernel_gen;
    }
}

void tlb_shootdown(struct pmap *pmap, vaddr_t sva, vaddr_t eva, int leave)
{
    uint32_t me = 1U << curcpu()->id;

    request_do(pmap, sva, eva, leave);

    /* the others, when they take the lock */
    if (pmap == kvm_space.pmap) {
        curcpu()->arch.kernel_gen = ++kernel_gen;
        return;
    }

    uint32_t others = __atomic_load_n(&pmap->cpus, __ATOMIC_ACQUIRE) & ~me;

    if (!others)
        return;

    request.pmap = pmap;
    request.sva = sva;
    request.eva = eva;
    request.leave = leave;
    __atomic_store_n(&request.pending, others, __ATOMIC_RELEASE);

    for (int i = 0; i < cpus_count; ++i)
        if (others & (1U << i))
            x86_lapic_ipi(cpus[i].arch.apic_id, X86_TLB_VECTOR | ICR_ASSERT);

    while (__atomic_load_n(&request.pending, __ATOMIC_ACQUIRE))
        asm volatile("pause");
}

/* the running CPU switched from the tables `from' (NULL: none) to `to' */
void pmap_loaded(struct pmap *from, struct pmap *to)
{
    uint32_t me = 1U << curcpu()->id;

    if (from)
        __atomic_and_fetch(&from->cpus, ~me, __ATOMIC_RELEASE);

    __atomic_or_fetch(&to->cpus, me, __ATOMIC_RELEASE);
}
