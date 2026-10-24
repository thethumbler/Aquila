/** \file
 * x86: keeping the CPUs' TLBs up to date when a mapping changes.
 *
 * A CPU caches the mappings it uses in its TLB. When the page tables
 * change (a page is unmapped, made read-only, or replaced), the CPU that
 * changed them flushes its own TLB, asks the other CPUs that may have the
 * old mapping to flush theirs with an IPI, and waits until they have. The
 * page can't be reused while another CPU may still write to it.
 *
 * For a process's page tables, the CPUs that may have the old mapping are
 * those the tables are loaded on (`pmap->cpus`, which `pmap_switch()`
 * keeps up to date). Only the CPU with the kernel lock changes page
 * tables, so there is one request at a time. The other CPUs do it without
 * the lock (someone else holds it): from the IPI's handler, or, if they
 * are waiting for the lock with interrupts off, from that wait
 * (`arch_lock_spin()`).
 *
 * The kernel's mappings, shared by all page tables, are in every CPU's
 * TLB, but the other CPUs aren't asked to flush them. Outside the lock, a
 * CPU only touches memory that was mapped before it last took the lock,
 * and that stays mapped while it uses it (its idle stack, its thread's
 * kernel stack, its struct cpu, the lock, the page tables it has loaded).
 * Instead, a CPU flushes its TLB when it takes the lock, if the kernel's
 * mappings changed since it last did (`kernel_gen`).
 *
 * Reloading CR3 flushes every mapping but the global ones. The only
 * global pages are x86_64's direct map, which never changes.
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

/* above this many pages, the whole TLB is flushed instead */
#define FLUSH_PAGES_MAX     32

#define ICR_ASSERT          0x4000

/* how many times the kernel's mappings changed; each CPU's TLB has
 * the kernel's mappings of one of these generations
 * (cpu->arch.kernel_gen) */
static uint64_t kernel_gen;

/* the request to the other CPUs: flush [sva, eva) of pmap, or leave pmap
 * for the kernel's page tables */
static struct {
    struct pmap *pmap;
    vaddr_t sva, eva;
    int leave;
    volatile uint32_t pending;  /* the CPUs that haven't done it yet */
} request;

/** Flushes [sva, eva) from the running CPU's TLB. */
static void flush(vaddr_t sva, vaddr_t eva)
{
    if ((eva - sva) / PAGE_SIZE > FLUSH_PAGES_MAX) {
        tlb_flush();
        return;
    }

    for (vaddr_t va = sva; va < eva; va += PAGE_SIZE)
        asm volatile("invlpg (%0)" :: "r"(va) : "memory");
}

/** Does what a request asks of the running CPU: flush the range, if the
 * CPU may have it, or switch away from page tables that are going away. */
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

/** Does the pending request if it is for the running CPU. It is called
 * from the TLB IPI's handler, and while waiting for the kernel lock. */
void x86_tlb_ipi(void)
{
    uint32_t me = 1U << curcpu()->id;

    if (!(__atomic_load_n(&request.pending, __ATOMIC_ACQUIRE) & me))
        return;

    request_do(request.pmap, request.sva, request.eva, request.leave);
    curcpu()->arch.tlb_flushes++;
    __atomic_and_fetch(&request.pending, ~me, __ATOMIC_RELEASE);
}

/* Runs in the kernel lock's wait loop: does a pending TLB request, so
 * that the CPU holding the lock isn't kept waiting for this one. */
void arch_lock_spin(void)
{
    x86_tlb_ipi();
    asm volatile("pause");
}

/* Runs when the running CPU takes the kernel lock: flushes its TLB if
 * the kernel's mappings changed since it last held the lock. */
void arch_lock_taken(void)
{
    struct cpu *cpu = curcpu();

    if (cpu->arch.kernel_gen != kernel_gen) {
        tlb_flush();
        cpu->arch.kernel_gen = kernel_gen;
    }
}

/*
 * Flushes a range of an address space from every TLB that may have it,
 * after its mappings changed, and waits until the other CPUs have done
 * it. The caller holds the kernel lock.
 */
void tlb_shootdown(struct pmap *pmap, vaddr_t sva, vaddr_t eva, int leave)
{
    uint32_t me = 1U << curcpu()->id;

    request_do(pmap, sva, eva, leave);

    /* the kernel's mappings: the other CPUs flush when they take the
     * lock */
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

/*
 * Records that the running CPU switched page tables, so that TLB requests
 * go to the CPUs that may have a mapping cached.
 */
void pmap_loaded(struct pmap *from, struct pmap *to)
{
    uint32_t me = 1U << curcpu()->id;

    if (from)
        __atomic_and_fetch(&from->cpus, ~me, __ATOMIC_RELEASE);

    __atomic_or_fetch(&to->cpus, me, __ATOMIC_RELEASE);
}
