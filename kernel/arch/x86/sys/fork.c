#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <bits/errno.h>

#include "sys.h"

int arch_proc_fork(thread_t *thread, proc_t *fork)
{
    printk("arch_proc_fork(fork=%p)\n", fork);

    int err = 0;
    x86_proc_t   *pparch = thread->owner->arch;
    x86_thread_t *ptarch = thread->arch;
    x86_proc_t   *fparch = NULL;    //kmalloc(sizeof(x86_proc_t));
    x86_thread_t *ftarch = NULL;    //kmalloc(sizeof(x86_thread_t));

    if (!(fparch = kmalloc(sizeof(x86_proc_t)))) {
        /* Failed to allocate fork process arch structure */
        err = -ENOMEM;
        goto free_resources;
    }

    if (!(ftarch = kmalloc(sizeof(x86_thread_t)))) {
        /* Failed to allocate fork thread arch structure */
        err = -ENOMEM;
        goto free_resources;
    }

    uintptr_t cur_proc_pd = pparch->pd;
    uintptr_t new_proc_pd = get_new_page_directory();
    
    if (!new_proc_pd) {
        /* Failed to allocate page directory */
        err = -ENOMEM;
        goto free_resources;
    }

    pmman.copy_fork_mapping(cur_proc_pd, new_proc_pd);

    /* Setup kstack */
    uintptr_t fkstack_base = (uintptr_t) kmalloc(KERN_STACK_SIZE);
    ftarch->kstack = fkstack_base + KERN_STACK_SIZE;

    /* Copy registers */
    regs_t *fork_regs = (void *) (ftarch->kstack - (ptarch->kstack - (uintptr_t) ptarch->regs));
    ftarch->regs = fork_regs;

    /* Copy kstack */
    memcpy((void *) fkstack_base, (void *) (ptarch->kstack - KERN_STACK_SIZE), KERN_STACK_SIZE);

    extern void x86_fork_return();
    ftarch->eip = (uintptr_t) x86_fork_return;
    ftarch->esp = (uintptr_t) fork_regs;

    fparch->pd = new_proc_pd;

    fork->arch = fparch;

    thread_t *fthread = (thread_t *) fork->threads.head->value;
    fthread->arch = ftarch;

    ftarch->fpu_enabled = 0; /* XXX */
    ftarch->fpu_context = NULL; /* XXX */

    return 0;

free_resources:
    if (fparch)
        kfree(fparch);
    if (ftarch)
        kfree(ftarch);
    return err;
}
