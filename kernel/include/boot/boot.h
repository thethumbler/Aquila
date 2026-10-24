#ifndef _BOOT_H
#define _BOOT_H

#include <core/system.h>
#include <sys/elf.h>

typedef struct {
    uintptr_t paddr;    /* physical, arch_phys_map() maps it */
    size_t size;
    char *cmdline;
} module_t;

enum mmap_type {
    MMAP_USABLE   = 1,
    MMAP_RESERVED = 2
};

typedef struct {
    enum mmap_type type;
    uintptr_t start;
    uintptr_t end;
} mmap_t;

struct boot {
    char *cmdline;
    uintptr_t total_mem;
    
    int modules_count;
    int mmap_count;
    module_t *modules;
    mmap_t *mmap;

    elf_shdr_t *shdr;
    uint32_t shdr_num;

    elf_shdr_t *symtab;
    size_t symnum;

    elf_shdr_t *strtab;
};

#endif
