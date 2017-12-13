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
#include <core/string.h>
#include <core/arch.h>

#include <sys/proc.h>
#include <sys/elf.h>

/* Loads an elf file into a new process skeleton */
proc_t *load_elf(const char *fn)
{
    void *arch_specific_data = arch_load_elf();

    struct fs_node *file = vfs.find(fn);
    if (!file) return NULL;

    elf32_hdr_t hdr;
    vfs.read(file, 0, sizeof(hdr), &hdr);

    /* Check header */
    if (!(hdr.magic[0] == ELFMAG0 && hdr.magic[1] == ELFMAG1 && hdr.magic[2] == ELFMAG2 && hdr.magic[3] == ELFMAG3))
        return NULL;

    uintptr_t proc_heap = 0;
    size_t offset = hdr.shoff;
    
    for (int i = 0; i < hdr.shnum; ++i) {
        elf32_section_hdr_t shdr;
        vfs.read(file, offset, sizeof(shdr), &shdr);

        if (shdr.flags & SHF_ALLOC) {
            /* FIXME add some out-of-bounds handling code here */
            pmman.map(shdr.addr, shdr.size, URWX);  /* FIXME URWX, are you serious? */
        
            if (shdr.type == SHT_PROGBITS) {
                vfs.read(file, shdr.off, shdr.size, (void *) shdr.addr);
            } else {
                memset((void *) shdr.addr, 0, shdr.size);
            }

            if (shdr.addr + shdr.size > proc_heap)
                proc_heap = shdr.addr + shdr.size;
        }

        offset += hdr.shentsize;
    }

    pmman.map(USER_STACK_BASE, USER_STACK_SIZE, URW);

    proc_t *proc = new_proc();
    //proc->name = strdup(file->name);
    proc->name = strdup(fn);
    proc->heap_start = proc_heap;
    proc->heap = proc_heap;
    proc->entry = hdr.entry;

    arch_init_proc(arch_specific_data, proc);
    arch_load_elf_end(arch_specific_data);

    return proc;
}

/* Loads an elf file into an existing process skeleton */
proc_t *load_elf_proc(proc_t *proc, const char *fn)
{
    struct fs_node *file = vfs.find(fn);
    if (!file) return NULL;

    pmman.unmap_full(0, proc->heap);

    elf32_hdr_t hdr;
    vfs.read(file, 0, sizeof(hdr), &hdr);

    /* Check header */
    if (!(hdr.magic[0] == ELFMAG0 && hdr.magic[1] == ELFMAG1 && hdr.magic[2] == ELFMAG2 && hdr.magic[3] == ELFMAG3))
        return NULL;

    uintptr_t proc_heap = 0;
    size_t offset = hdr.shoff;
    
    for (int i = 0; i < hdr.shnum; ++i) {
        elf32_section_hdr_t shdr;
        vfs.read(file, offset, sizeof(shdr), &shdr);
        
        if (shdr.flags & SHF_ALLOC) {
            /* FIXME add some out-of-bounds handling code here */
            pmman.map(shdr.addr, shdr.size, URWX);  /* FIXME URWX, are you serious? */

            if (shdr.type == SHT_PROGBITS) {
                vfs.read(file, shdr.off, shdr.size, (void *) shdr.addr);
            } else {
                memset((void *) shdr.addr, 0, shdr.size);
            }

            if (shdr.addr + shdr.size > proc_heap)
                proc_heap = shdr.addr + shdr.size;
        }

        offset += hdr.shentsize;
    }

    kfree(proc->name);
    //proc->name = strdup(file->name);
    proc->name = strdup(fn);
    proc->heap_start = proc_heap;
    proc->heap = proc_heap;
    proc->entry = hdr.entry;

    return proc;
}
