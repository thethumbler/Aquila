#ifndef _PAGING_X86_64_H
#define _PAGING_X86_64_H

#include <core/system.h>

/* 
 * Page Management Level 4 (PML4)
 * Table 4-12. Use of CR3 with IA-32e Paging and CR4.PCIDE = 0
 */

typedef union __pml4 {
    struct {
        size_t : 3;
        size_t page_level_write_through : 1;
        size_t page_level_cache_disable : 1;
        size_t : 7;
        uintptr_t phys_addr : 52;
    } __packed;
    uint64_t raw;
} __packed __pml4_t;

/* 
 * PML4 Entry (Directory Table)
 * Table 4-14. Format of an IA-32e PML4 Entry (PML4E) 
 *  that References a Page-Directory-Pointer Table
 */

typedef union __dirtbl {
    struct {
        size_t present : 1;
        size_t write : 1;
        size_t user : 1;
        size_t page_level_write_through : 1;
        size_t page_level_cache_disable : 1;
        size_t accessed : 1;
        size_t : 1;
        size_t page_size : 1;
        size_t : 4;
        uintptr_t phys_addr : 52;
    } __packed;
    uint64_t raw;
} __packed __dirtbl_t;

/*
 * Page Directory (Directory) Structure
 * Table 4-16. Format of an IA-32e Page-Directory-Pointer-Table
 *  Entry (PDPTE) that References a Page Directory
 */

typedef union __directory {
    struct {
        size_t present : 1;
        size_t write : 1;
        size_t user : 1;
        size_t page_level_write_through : 1;
        size_t page_level_cache_disable : 1;
        size_t : 7;
        uintptr_t phys_addr : 52;
    } __packed;
    uint64_t raw;
} __packed __directory_t;

/*
 * Page Directory Entry (Table) Structure
 * Table 4-18. Format of an IA-32e Page-Directory Entry 
 *  that References a Page Table
 */

typedef union __table {
    struct {
        size_t present : 1;
        size_t write : 1;
        size_t user : 1;
        size_t page_level_write_through : 1;
        size_t page_level_cache_disable : 1;
        size_t accessed : 1;
        size_t : 1;
        size_t page_size : 1;
        size_t : 4;
        uintptr_t phys_addr : 52;
    } __packed;
    uint64_t raw;
} __packed __table_t;


/*
 * Page Table Entry (Page) Structure
 * Table 4-19. Format of an IA-32e Page-Table Entry
 *  that Maps a 4-KByte Page
 */

typedef union __page {
    struct {
        size_t present : 1;
        size_t write : 1;
        size_t user : 1;
        size_t page_level_write_through : 1;
        size_t page_level_cache_disable : 1;
        size_t accessed : 1;
        size_t dirty : 1;
        size_t memory_type: 1;
        size_t global : 1;
        size_t : 3;
        uintptr_t phys_addr : 52;
    } __packed;
    uint64_t raw;
} __packed __page_t;

#define LOWER_TABLE_BOUNDARY(ptr) ((ptr) & ~TABLE_MASK)
#define UPPER_TABLE_BOUNDARY(ptr) (((ptr) + TABLE_MASK) & ~TABLE_MASK)

#define GET_PHYS_ADDR(s) (((s)->phys_addr) << 12U)

typedef union {
    struct {
        size_t offset    : 12;
        size_t table     : 9;
        size_t directory : 9;
        size_t dirtbl    : 9;
        size_t pml4      : 9;
        size_t           : 16;
    } __packed;
    uint64_t raw;
} __packed __virtaddr_t;

#endif /* ! _PAGING_X86_64_H */
