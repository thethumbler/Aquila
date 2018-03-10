#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <bits/errno.h>

#include "sys.h"

int arch_sys_fork(proc_t *proc)
{
    x86_proc_t *orig_arch = cur_proc->arch;
    x86_proc_t *fork_arch = kmalloc(sizeof(x86_proc_t));

    if (!fork_arch) {   /* Failed to allocate fork arch structure */
        return -ENOMEM;
    }

    uintptr_t cur_proc_pd = orig_arch->pd;
    uintptr_t new_proc_pd = get_new_page_directory();
    
    if (!new_proc_pd) { /* Failed to allocate page directory */
        kfree(fork_arch);
        return -ENOMEM;
    }

    pmman.copy_fork_mapping(cur_proc_pd, new_proc_pd);

#if 0

    switch_page_directory(new_proc_pd);

    int tables_count = ((uintptr_t) proc->heap + TABLE_MASK)/TABLE_SIZE;

    uint32_t *tables_buf = kmalloc(tables_count * sizeof(uint32_t));
    uint32_t *page_buf = kmalloc(PAGE_SIZE);

    memset(tables_buf, 0, tables_count * sizeof(uint32_t));
    memset(page_buf,  0, PAGE_SIZE);

    pmman.memcpypv(tables_buf, (void *) cur_proc_pd, tables_count * sizeof(uint32_t));

    for (int i = 0; i < tables_count; ++i) {
        if (tables_buf[i] != 0) {
            pmman.memcpypv((void *) page_buf, (void *) (tables_buf[i] & ~PAGE_MASK), PAGE_SIZE);
            for (uint32_t j = 0; j < 1024UL; ++j) {
                if (page_buf[j] != 0) {
                    pmman.map(j * PAGE_SIZE, PAGE_SIZE, URWX);
                    pmman.memcpypv((void *) (j * PAGE_SIZE), (void *) (page_buf[j] & ~PAGE_MASK), PAGE_SIZE);
                }
            }
        }
    }
    
    kfree(tables_buf);
    kfree(page_buf);

    /* Setup kstack */
    uintptr_t fork_kstack_base = (uintptr_t) kmalloc(KERN_STACK_SIZE);
    fork_arch->kstack = fork_kstack_base + KERN_STACK_SIZE;

    /* Copy registers */
    void *fork_regs = (void *)(fork_arch->kstack - sizeof(regs_t));
    memcpy(fork_regs, orig_arch->regs, sizeof(regs_t));
    fork_arch->regs = fork_regs;
    
    extern void x86_fork_return();
    fork_arch->eip = (uintptr_t) x86_fork_return;
    fork_arch->esp = (uintptr_t) fork_regs;
    //fork_arch->ebp = orig_arch->regs->ebp;
    //fork_arch->eflags = orig_arch->regs->eflags;

    /* Copy stack */
    pmman.map(USER_STACK_BASE, USER_STACK_SIZE, URWX);

#define STACK_BUF   (1024UL*1024)

    /* Copying stack in SATCK_BUF blocks */
    void *stack_buf = kmalloc(STACK_BUF);

    for (uint32_t i = 0; i < USER_STACK_SIZE/STACK_BUF; ++i) {
        switch_page_directory(cur_proc_pd);
        memcpy(stack_buf, (char *) USER_STACK_BASE + i * STACK_BUF, STACK_BUF);
        switch_page_directory(new_proc_pd);
        memcpy((char *) USER_STACK_BASE + i * STACK_BUF, stack_buf, STACK_BUF);
        switch_page_directory(cur_proc_pd);
    }

    kfree(stack_buf);
#endif

    /* Setup kstack */
    uintptr_t fork_kstack_base = (uintptr_t) kmalloc(KERN_STACK_SIZE);
    fork_arch->kstack = fork_kstack_base + KERN_STACK_SIZE;

    /* Copy registers */
    regs_t *fork_regs = (void *) (fork_arch->kstack - (orig_arch->kstack - (uintptr_t) orig_arch->regs));  //(fork_arch->kstack - sizeof(regs_t));
    fork_arch->regs = fork_regs;

    /* Copy kstack */
    memcpy((void *) fork_kstack_base, (void *) (orig_arch->kstack - KERN_STACK_SIZE), KERN_STACK_SIZE);

    extern void x86_fork_return();
    fork_arch->eip = (uintptr_t) x86_fork_return;
    fork_arch->esp = (uintptr_t) fork_regs;
    //fork_arch->ebp = orig_arch->regs->ebp;
    //fork_arch->eflags = orig_arch->regs->eflags;

    fork_arch->pd = new_proc_pd;
    proc->arch = fork_arch;

    fork_arch->fpu_enabled = 0; /* XXX */
    fork_arch->fpu_context = NULL; /* XXX */

    return 0;
}
