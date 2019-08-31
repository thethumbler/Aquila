/*
 *          ELF format loader
 *
 *
 *  This file is part of Aquila OS and is released under
 *  the terms of GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */


#include <core/system.h>
#include <core/panic.h>

#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/elf.h>
#include <mm/vm.h>

static int binfmt_elf32_load(struct proc *proc, struct inode *inode)
{
    int err = 0;

    struct vm_space *vm_space = &proc->vm_space;

    struct elf32_hdr hdr;

    if (vfs_read(inode, 0, sizeof(hdr), &hdr) != sizeof(hdr))
        goto e_inval;

    uintptr_t proc_heap = 0;
    size_t offset = hdr.e_phoff;
    
    for (int i = 0; i < hdr.e_phnum; ++i) {
        struct elf32_phdr phdr;
        
        if (vfs_read(inode, offset, sizeof(phdr), &phdr) != sizeof(phdr))
            goto e_inval;

        if (phdr.p_type == PT_LOAD) {
            vaddr_t base  = phdr.p_vaddr;
            size_t filesz = phdr.p_filesz;
            size_t memsz  = phdr.p_memsz;
            size_t off    = phdr.p_offset;

            /* make sure vaddr is aligned */
            if (base & PAGE_MASK) {
                memsz  += base & PAGE_MASK;
                filesz += base & PAGE_MASK;
                off    -= base & PAGE_MASK;
                base    = PAGE_ALIGN(base);
            }

            /* page align size */
            memsz = PAGE_ROUND(memsz);

            struct vm_entry *vm_entry = vm_entry_new();
            if (!vm_entry) goto e_nomem;

            vm_entry->base = base;
            vm_entry->size = memsz;
            vm_entry->off  = off;

            /* access flags */
            vm_entry->flags |= phdr.p_flags & PF_R? VM_UR : 0;
            vm_entry->flags |= phdr.p_flags & PF_W? VM_UW : 0;
            vm_entry->flags |= phdr.p_flags & PF_X? VM_UX : 0;

            /* TODO use W^X */

            vm_entry->qnode = enqueue(&vm_space->vm_entries, vm_entry);

            if (!vm_entry->qnode)
                goto e_nomem;

            vm_entry->vm_object = vm_object_inode(inode);

            if (!vm_entry->vm_object)
                goto e_nomem;

            if (vm_entry->flags & VM_UW)
                vm_entry->flags |= VM_COPY;

            vm_object_incref(vm_entry->vm_object);

            if (base + memsz > proc_heap)
                proc_heap = base + memsz;

            /* handle bss */
            if (phdr.p_memsz != phdr.p_filesz) {
                vaddr_t bss = base + filesz;

                vaddr_t bss_init_end = PAGE_ROUND(base + filesz);

                if (vm_entry->base + vm_entry->size > bss_init_end) {
                    size_t sz = bss_init_end - vm_entry->base;

                    struct vm_entry *split = vm_entry_new();

                    split->base = bss_init_end;
                    split->size = vm_entry->size - sz;
                    split->flags = vm_entry->flags;
                    split->off = 0;

                    split->qnode = enqueue(&vm_space->vm_entries, split);

                    vm_entry->size = sz;
                }

                /* fault in the page */
                memset((void *) bss, 0, bss_init_end-bss);
            }
        }

        offset += hdr.e_phentsize;
    }

    proc->heap_start = proc_heap;
    proc->heap       = proc_heap;
    proc->entry      = hdr.e_entry;

    return err;

e_nomem:
    err = -ENOMEM;
    goto error;

e_inval:
    err = -EINVAL;
    goto error;

error:
    printk("err %d\n", -err);
    panic("TODO");
}

#if 0
static int binfmt_elf64_load(struct proc *proc, struct inode *file)
{
    int err = 0;

    struct elf64_hdr hdr;
    vfs_read(file, 0, sizeof(hdr), &hdr);

    uintptr_t proc_heap = 0;
    size_t offset = hdr.shoff;
    
    for (int i = 0; i < hdr.shnum; ++i) {
        struct elf64_section_hdr shdr;
        vfs_read(file, offset, sizeof(shdr), &shdr);
        
        if (shdr.flags & SHF_ALLOC) {
            struct vmr *vmr = kmalloc(sizeof(struct vmr));
            memset(vmr, 0, sizeof(struct vmr));

            vmr->base  = shdr.addr;
            vmr->size  = shdr.size;

            /* access flags */
            vmr->flags |= VM_UR;
            vmr->flags |= shdr.flags & SHF_WRITE ? VM_UW : 0;
            vmr->flags |= shdr.flags & SHF_EXEC ? VM_UX : 0;
            //vmr->flags = VM_URWX;   /* FIXME */

            vmr->qnode = enqueue(&proc->vmr, vmr);

            if (shdr.type == SHT_PROGBITS) {
                vmr->flags |= VM_FILE;
                vmr->off   = shdr.off;
                vmr->inode = file;
                //vfs_read(vmr->inode, vmr->off, vmr->size, (void *) vmr->base);
            } else {
                vmr->flags |= VM_ZERO;
                //memset((void *) vmr->base, 0, vmr->size);
            }

            if (shdr.addr + shdr.size > proc_heap)
                proc_heap = shdr.addr + shdr.size;
        }

        offset += hdr.shentsize;
    }

    proc->heap_start = proc_heap;
    proc->heap = proc_heap;
    proc->entry = hdr.entry;

    return err;
}
#endif

int binfmt_elf_check(struct inode *inode)
{
    struct elf32_hdr hdr;
    vfs_read(inode, 0, sizeof(hdr), &hdr);

    /* Check header */
    if (hdr.e_ident[EI_MAG0] == ELFMAG0 &&
        hdr.e_ident[EI_MAG1] == ELFMAG1 &&
        hdr.e_ident[EI_MAG2] == ELFMAG2 &&
        hdr.e_ident[EI_MAG3] == ELFMAG3)
        return 0;

    return -ENOEXEC;
}

int binfmt_elf_load(struct proc *proc, const char *path __unused, struct inode *inode)
{
    int err = 0;

    struct elf32_hdr hdr;

    if (vfs_read(inode, 0, sizeof(hdr), &hdr) != sizeof(hdr))
        return -EINVAL;

    switch (hdr.e_ident[EI_CLASS]) {
        case ELFCLASS32:
            return binfmt_elf32_load(proc, inode);
    }

    return -EINVAL;
}
