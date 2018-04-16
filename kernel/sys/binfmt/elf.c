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
#include <sys/proc.h>
#include <sys/elf.h>
#include <mm/vm.h>

int binfmt_elf_check(struct inode *file)
{
    elf32_hdr_t hdr;
    vfs_read(file, 0, sizeof(hdr), &hdr);

    /* Check header */
    if (hdr.magic[0] == ELFMAG0 && hdr.magic[1] == ELFMAG1 && hdr.magic[2] == ELFMAG2 && hdr.magic[3] == ELFMAG3)
        return 0;

    return -ENOEXEC;
}

int binfmt_elf_load(proc_t *proc, struct inode *file)
{
    int err = 0;

    elf32_hdr_t hdr;
    vfs_read(file, 0, sizeof(hdr), &hdr);

    uintptr_t proc_heap = 0;
    size_t offset = hdr.shoff;
    
    for (int i = 0; i < hdr.shnum; ++i) {
        elf32_section_hdr_t shdr;
        vfs_read(file, offset, sizeof(shdr), &shdr);
        
        if (shdr.flags & SHF_ALLOC) {
            struct vmr *vmr = kmalloc(sizeof(struct vmr));
            memset(vmr, 0, sizeof(struct vmr));

            vmr->base  = shdr.addr;
            vmr->size  = shdr.size;

            /* access flags */
            /*
            vmr->flags |= VM_UR;
            vmr->flags |= shdr.flags & SHF_WRITE ? VM_UW : 0;
            vmr->flags |= shdr.flags & SHF_EXEC ? VM_UX : 0;
            */
            vmr->flags = VM_URWX;   /* FIXME */

            vmr->qnode = enqueue(&proc->vmr, vmr);

            /* FIXME add some out-of-bounds handling code here */
            //pmman.map(vmr->base, vmr->size, vmr->flags);

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
