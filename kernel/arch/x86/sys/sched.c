#include <core/system.h>
#include <core/arch.h>
#include <core/chipset.h>

#include <sys/proc.h>
#include <sys/sched.h>
#include <mm/mm.h>

#include "sys.h"

uint32_t timer_freq = 100;
uint32_t timer_ticks = 0;
uint32_t timer_sub_ticks = 0;

static void x86_sched_handler(struct x86_regs *r) 
{
    if (!kidle) {
        x86_thread_t *arch = (x86_thread_t *) cur_thread->arch;

        extern uintptr_t x86_read_ip();

        volatile uintptr_t ip = 0, sp = 0, bp = 0;    
#if ARCH_BITS==32
        asm volatile("mov %%esp, %0":"=r"(sp)); /* read esp */
        asm volatile("mov %%ebp, %0":"=r"(bp)); /* read ebp */
#else
        asm volatile("mov %%rsp, %0":"=r"(sp)); /* read rsp */
        asm volatile("mov %%rbp, %0":"=r"(bp)); /* read rbp */
#endif
        ip = x86_read_ip();

        if (ip == (uintptr_t) -1) {    /* Done switching */
            return;
        }

#if ARCH_BITS==32
        arch->eip = ip;
        arch->esp = sp;
        arch->ebp = bp;
#else
        arch->rip = ip;
        arch->rsp = sp;
        arch->rbp = bp;
#endif
    }

    schedule();
}

void arch_sched_init()
{
    chipset_timer_setup(1, x86_sched_handler);
}

void __arch_idle()
{
    for (;;) {
        asm volatile("sti; hlt; cli;");
    }
}

static char __idle_stack[8192] __aligned(16);

void arch_idle()
{
    cur_thread = NULL;

    uintptr_t esp = VMA(0x100000);
    x86_kernel_stack_set(esp);
    uintptr_t stack = (uintptr_t) __idle_stack + 8192;
    extern void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));
    x86_goto((uintptr_t) __arch_idle, stack, stack);
}

static void __arch_cur_thread_kill(void)
{
    thread_kill(cur_thread);    /* Will set the stack to VMA(0x100000) */
    kfree(cur_thread);
    cur_thread = NULL;
    kernel_idle();
}

void arch_cur_thread_kill(void)
{
    uintptr_t stack = (uintptr_t) __idle_stack + 8192;

    extern void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));
    x86_goto((uintptr_t) __arch_cur_thread_kill, stack, stack);
}

void arch_sleep()
{
    extern void x86_sleep();
    x86_sleep();
}

