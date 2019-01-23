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
#include <sys/elf.h>
#include <mm/vm.h>

int binfmt_elf_check(struct inode *inode)
{
    struct elf32_hdr hdr;
    vfs_read(inode, 0, sizeof(hdr), &hdr);

    /* Check header */
    if (hdr.magic[0] == ELFMAG0 && hdr.magic[1] == ELFMAG1 && hdr.magic[2] == ELFMAG2 && hdr.magic[3] == ELFMAG3)
        return 0;

    return -ENOEXEC;
}

static int binfmt_elf32_load(struct proc *proc, struct inode *inode)
{
    int err = 0;

    struct elf32_hdr hdr;
    vfs_read(inode, 0, sizeof(hdr), &hdr);

    uintptr_t proc_heap = 0;
    size_t offset = hdr.shoff;
    
    //proc->vmr.flags = QUEUE_TRACE;

    for (int i = 0; i < hdr.shnum; ++i) {
        struct elf32_section_hdr shdr;
        vfs_read(inode, offset, sizeof(shdr), &shdr);
        
        if (shdr.flags & SHF_ALLOC) {
            struct vm_entry *vm_entry = kmalloc(sizeof(struct vm_entry));

            if (!vm_entry)
                goto error;

            memset(vm_entry, 0, sizeof(struct vm_entry));

            vm_entry->base  = shdr.addr;
            vm_entry->size  = shdr.size;

            /* access flags */
            vm_entry->flags |= VM_UR;
            vm_entry->flags |= shdr.flags & SHF_WRITE ? VM_UW : 0;
            vm_entry->flags |= shdr.flags & SHF_EXEC ? VM_UX : 0;

            /* TODO use W^X */

#if 1
            vm_entry->qnode = enqueue(&proc->vm_space.vm_entries, vm_entry);

            if (!vm_entry->qnode)
                goto error;
#endif

            if (shdr.type == SHT_PROGBITS) {
                vm_entry->flags |= VM_FILE;
                vm_entry->off    = shdr.off;
                vm_entry->inode  = inode;
            } else {
                vm_entry->flags |= VM_ZERO;
            }

            if (shdr.addr + shdr.size > proc_heap)
                proc_heap = shdr.addr + shdr.size;
        }

        offset += hdr.shentsize;
    }

    proc->heap_start = proc_heap;
    proc->heap       = proc_heap;
    proc->entry      = hdr.entry;

    return err;

error:
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

int binfmt_elf_load(struct proc *proc, const char *path __unused, struct inode *inode)
{
    int err = 0;

    struct elf32_hdr hdr;
    vfs_read(inode, 0, sizeof(hdr), &hdr);

    switch (hdr.class) {
        case 1:
            return binfmt_elf32_load(proc, inode);
    }

    return -EINVAL;
}
