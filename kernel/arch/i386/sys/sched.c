#include <core/arch.h>
#include <core/platform.h>
#include <core/time.h>
#include <mm/mm.h>
#include <sys/proc.h>
#include <sys/sched.h>

#include "sys.h"

static uint32_t timer_period = 0;
static uint64_t timer_ticks = 0;

uint64_t arch_rtime_ns(void)
{
    return timer_ticks * timer_period;
}

uint64_t arch_rtime_us(void)
{
    return timer_ticks * timer_period / 1000ULL;
}

uint64_t arch_rtime_ms(void)
{
    return timer_ticks * timer_period / 1000000ULL;
}

static uint64_t first_measured_time = 0;

static void x86_sched_handler(struct x86_regs *r) 
{
    /* we check time every 2^16 ticks */
    if (!(timer_ticks & 0xFFFF)) {
        if (!timer_ticks) {
            struct timespec ts = {0};
            gettime(&ts);
            first_measured_time = ts.tv_sec;
        } else {
            struct timespec ts = {0};
            gettime(&ts);

            uint64_t measured_time = ts.tv_sec - first_measured_time;
            uint64_t calculated_time = arch_rtime_ms() / 1000;

            int32_t delta = calculated_time - measured_time;

            if (ABS(delta) > 1) {
                printk("warning: calculated time differs from measured time by %c%d seconds\n", delta < 0? '-' : '+', ABS(delta));
                printk("calculated time: %d seconds\n", calculated_time);
                printk("measured time: %d seconds\n", measured_time);

                /* TODO: attempt to correct time */
            }
        }
    }

    ++timer_ticks;

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
    timer_period = platform_timer_setup(2000000, x86_sched_handler);
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
