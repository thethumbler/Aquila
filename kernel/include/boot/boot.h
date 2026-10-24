#ifndef _BOOT_H
#define _BOOT_H

#include <core/system.h>
#include <sys/elf.h>

/*
 * What the boot loader passed the kernel, in the same form whatever the
 * boot loader (Multiboot on the PC, a devicetree on aarch64): the memory
 * map, the modules, the command line, and the kernel's symbols if they
 * were passed.
 */

/* A module the boot loader loaded: the RAM disk. */
typedef struct {
    uintptr_t paddr;    /* its physical address; arch_phys_map() maps it */
    size_t size;
    char *cmdline;
} module_t;

/* What a region of the memory map is. */
enum mmap_type {
    MMAP_USABLE   = 1,
    MMAP_RESERVED = 2
};

/* A region of the memory map, [start, end). */
typedef struct {
    enum mmap_type type;
    uintptr_t start;
    uintptr_t end;
} mmap_t;

/* Everything the boot loader passed. */
struct boot {
    char *cmdline;
    uintptr_t total_mem;    /* how much memory there is, in KiB */
    
    int modules_count;
    int mmap_count;
    module_t *modules;
    mmap_t *mmap;

    /* the kernel's ELF section headers, if the boot loader passed them,
     * and its symbol table and string table among them */
    elf_shdr_t *shdr;
    uint32_t shdr_num;

    elf_shdr_t *symtab;
    size_t symnum;          /* how many symbols */

    elf_shdr_t *strtab;
};

#endif
