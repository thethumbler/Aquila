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

int binfmt_elf_check(struct inode *file)
{
    printk("binfmt_elf_check(file=%p)\n", file);

    elf32_hdr_t hdr;
    vfs_read(file, 0, sizeof(hdr), &hdr);

    /* Check header */
    if (hdr.magic[0] == ELFMAG0 && hdr.magic[1] == ELFMAG1 && hdr.magic[2] == ELFMAG2 && hdr.magic[3] == ELFMAG3)
        return 0;

    return -ENOEXEC;
}

int binfmt_elf_load(proc_t *proc, struct inode *file)
{
    printk("binfmt_elf_load(proc=%p, file=%p)\n", proc, file);

    int err = 0;

    elf32_hdr_t hdr;
    vfs_read(file, 0, sizeof(hdr), &hdr);

    uintptr_t proc_heap = 0;
    size_t offset = hdr.shoff;
    
    for (int i = 0; i < hdr.shnum; ++i) {
        elf32_section_hdr_t shdr;
        vfs_read(file, offset, sizeof(shdr), &shdr);
        
        if (shdr.flags & SHF_ALLOC) {
            /* FIXME add some out-of-bounds handling code here */
            pmman.map(shdr.addr, shdr.size, URWX);  /* FIXME URWX, are you serious? */

            if (shdr.type == SHT_PROGBITS) {
                vfs_read(file, shdr.off, shdr.size, (void *) shdr.addr);
            } else {
                memset((void *) shdr.addr, 0, shdr.size);
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
