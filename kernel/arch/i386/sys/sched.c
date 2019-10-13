#include <core/arch.h>
#include <core/platform.h>
#include <mm/mm.h>
#include <sys/proc.h>
#include <sys/sched.h>

#include "sys.h"

uint32_t timer_freq = 62;
uint32_t timer_ticks = 0;
uint32_t timer_sub_ticks = 0;

static void x86_sched_handler(struct x86_regs *r) 
{
    ++timer_sub_ticks;
    if (timer_sub_ticks == timer_freq) {
        timer_ticks++;
        timer_sub_ticks = 0;
    }

    if (!kidle) {
        struct x86_thread *arch = (struct x86_thread *) curthread->arch;

        extern uintptr_t x86_read_ip(void);

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

void arch_sched_init(void)
{
    platform_timer_setup(20000, x86_sched_handler);
}

static void __arch_idle(void)
{
    for (;;) {
        asm volatile("sti; hlt; cli;");
    }
}

static char __idle_stack[8192] __aligned(16);

void arch_idle(void)
{
    curthread = NULL;

    uintptr_t esp = VMA(0x100000);
    x86_kernel_stack_set(esp);
    uintptr_t stack = (uintptr_t) __idle_stack + 8192;
    extern void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));
    x86_goto((uintptr_t) __arch_idle, stack, stack);
}

static void __arch_cur_thread_kill(void)
{
    thread_kill(curthread);    /* Will set the stack to VMA(0x100000) */
    kfree(curthread);
    curthread = NULL;
    kernel_idle();
}

void arch_cur_thread_kill(void)
{
    uintptr_t stack = (uintptr_t) __idle_stack + 8192;

    extern void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));
    x86_goto((uintptr_t) __arch_cur_thread_kill, stack, stack);
}

void arch_sleep(void)
{
    extern void x86_sleep(void);
    x86_sleep();
}

