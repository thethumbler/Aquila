/*
 *          ELF format loader
 */


#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>

#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/elf.h>
#include <mm/vm.h>

/* the kernel's own class and machine, the only ones it runs */
#if ARCH_BITS==32
#define ELF_CLASS       ELFCLASS32
#define ELF_MACHINE     EM_386
typedef struct elf32_hdr  elf_hdr;
typedef struct elf32_phdr elf_phdr;
#else
#define ELF_CLASS       ELFCLASS64
#define ELF_MACHINE     EM_X86_64
typedef struct elf64_hdr  elf_hdr;
typedef struct elf64_phdr elf_phdr;
#endif

static int binfmt_elf_load_segments(struct proc *proc, struct vnode *vnode)
{
    int err = 0;

    struct vm_space *vm_space = &proc->vm_space;

    elf_hdr hdr;

    if (vfs_read(vnode, 0, sizeof(hdr), &hdr) != sizeof(hdr))
        goto e_inval;

    if (hdr.e_ident[EI_CLASS] != ELF_CLASS || hdr.e_machine != ELF_MACHINE)
        goto e_inval;

    if (hdr.e_phentsize < sizeof(elf_phdr))
        goto e_inval;

    uintptr_t proc_heap = 0;
    size_t offset = hdr.e_phoff;

    for (int i = 0; i < hdr.e_phnum; ++i) {
        elf_phdr phdr;

        if (vfs_read(vnode, offset, sizeof(phdr), &phdr) != sizeof(phdr))
            goto e_inval;

        if (phdr.p_type == PT_LOAD) {
            vaddr_t base  = phdr.p_vaddr;
            size_t filesz = phdr.p_filesz;
            size_t memsz  = phdr.p_memsz;
            size_t off    = phdr.p_offset;

            /* the segment must lie entirely below the user stack (the
             * kernel writes to it below to clear the bss) and its file
             * offset must be congruent to its address modulo page size */
            if (filesz > memsz ||
                base + memsz < base ||
                PAGE_ROUND(base + memsz) > USER_STACK_BASE ||
                PAGE_ROUND(base + memsz) < base + memsz ||
                (off & PAGE_MASK) != (base & PAGE_MASK) ||
                PAGE_ALIGN(base) < proc_heap)  /* sorted, non overlapping */
                goto e_inval;

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

            if (!vm_entry->qnode) {
                kfree(vm_entry);
                goto e_nomem;
            }

            vm_entry->vm_object = vm_object_vnode(vnode);

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
                    if (!split) goto e_nomem;

                    split->base = bss_init_end;
                    split->size = vm_entry->size - sz;
                    split->flags = vm_entry->flags;
                    split->off = 0;

                    split->qnode = enqueue(&vm_space->vm_entries, split);

                    if (!split->qnode) {
                        kfree(split);
                        goto e_nomem;
                    }

                    vm_entry->size = sz;
                }

                /* fault in the page -- only writable segments, otherwise
                 * we would be writing to a read-only (shared) page */
                if (vm_entry->flags & VM_UW)
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
    /* entries already added are released by the caller */
    return err;
}


int binfmt_elf_check(struct vnode *vnode)
{
    struct elf32_hdr hdr;

    if (vfs_read(vnode, 0, sizeof(hdr), &hdr) != sizeof(hdr))
        return -ENOEXEC;

    /* Check header */
    if (hdr.e_ident[EI_MAG0] == ELFMAG0 &&
        hdr.e_ident[EI_MAG1] == ELFMAG1 &&
        hdr.e_ident[EI_MAG2] == ELFMAG2 &&
        hdr.e_ident[EI_MAG3] == ELFMAG3)
        return 0;

    return -ENOEXEC;
}

int binfmt_elf_load(struct proc *proc, const char *path __unused, struct vnode *vnode)
{
    return binfmt_elf_load_segments(proc, vnode);
}
