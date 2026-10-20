/*
 * CPU state on kernel entry and user-mode exceptions
 *
 * - the kernel must not trust EFLAGS.DF or the data segment registers
 *   left by user code
 * - user-mode exceptions and port I/O must signal the process instead
 *   of panicking the kernel
 * - SSE must be usable from user space
 * - the initial user stack must follow the i386 ABI alignment
 */

#include "../lib/aqtest.h"

#define SIGNALED(sig)   (((sig) << 8) | (sig))
#define EXITED(code)    ((code) << 8)

static char bigbuf[64 * 4096] __attribute__((aligned(4096)));

static void t_outb(void) { asm volatile("outb %%al, $0x80" :: "a"(0)); }
static void t_ud2(void)  { asm volatile("ud2"); }
static void t_div(void)  { volatile int z = 0; volatile int x = 5 / z; (void) x; }

/* gcc may target i686 without SSE for -m32 (Ubuntu does), which forbids
 * naming xmm0 in asm */
__attribute__((target("sse")))
static void t_sse(void)
{
    float f[4] __attribute__((aligned(16))) = {1, 2, 3, 4};

    asm volatile("movups (%0), %%xmm0\n"
                 "addps %%xmm0, %%xmm0\n"
                 "movups %%xmm0, (%0)" :: "r"(f) : "memory", "xmm0");

    if (f[3] != 8.0f)
        SC(SYS_exit, 1, 0, 0);
}

void aq_main(void)
{
    aq_begin("cpu");

    check("entry stack 16-byte aligned", aq_entry_esp & 15, 0);

    /* DF set while entering the kernel: the page fault handler zeroes the
     * new page with rep stos, which must still run forwards */
    asm volatile("std");
    bigbuf[5 * 4096 + 100] = 1;
    asm volatile("cld");

    int ok = 1;
    for (int i = 0; i < 4096; ++i)
        if (bigbuf[5 * 4096 + i] != (i == 100))
            ok = 0;

    check("page zeroed with DF set", ok, 1);

    /* null data segments while making a syscall */
    u32 r;
    asm volatile(
        "mov %%ds, %%bx\n"
        "xor %%ax, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %1, %%eax\n"
        "int $0x80\n"
        "mov %%bx, %%cx\n"
        "mov %%cx, %%ds\n"
        "mov %%cx, %%es\n"
        "mov %%cx, %%fs\n"
        "mov %%cx, %%gs\n"
        : "=a"(r) : "i"(SYS_getpid) : "ebx", "ecx", "memory");

    check("syscall with null segments", r > 0, 1);

    /* user-mode exceptions and port I/O are signals, not panics */
    check("port I/O -> SIGSEGV", aq_child_status(t_outb), SIGNALED(SIGSEGV));
    check("ud2 -> SIGILL", aq_child_status(t_ud2), SIGNALED(SIGILL));
    check("divide by zero -> SIGFPE", aq_child_status(t_div), SIGNALED(SIGFPE));
    check("SSE usable", aq_child_status(t_sse), EXITED(0x42));

    aq_end();
}
