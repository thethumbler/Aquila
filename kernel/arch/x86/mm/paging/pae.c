/**********************************************************************
 *                 Intel PAE Paging Mode Handler
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016-2017 Mohamed Anwar <mohamed_anwar@opmbx.org>
 *
 *  References:
 *      [1] - Intel(R) 64 and IA-32 Architectures Software Developers Manual
 *          - Volume 3, System Programming Guide
 *          - Chapter 4, Paging
 *
 */

#include <core/system.h>
#include <core/printk.h>
#include <ds/queue.h>

/*
 *  Page Directory Pointer Table (PDPT) Structure
 *  References:
 *      - [1] Table 4-7. Use of CR3 with PAE Paging
 */

struct __directory_table {
    size_t __ignored_1 : 5;
    uintptr_t phys_addr : 27;
} __packed;

/*
 *  Page Directory (Directory) Structure
 *  References:
 *      - [1] Table 4-8. Format of a PAE Page-Directory-Pointer-Table Entry (PDPTE)
 */

struct __directory {
    size_t present : 1;
    size_t __reserved_0 : 2;
    size_t page_level_write_through : 1;
    size_t page_level_cache_disable : 1;
    size_t __reserved_1 : 4;
    size_t __ignored: 3;
    uintptr_t phys_addr : 42;
} __packed;

/*
 *  Page Directory Entry (Table) Structure
 *  References:
 *      - [1] Table 4-10. Format of a PAE Page-Directory Entry that References a Page Table
 */

struct __table {
    size_t present : 1;
    size_t write : 1;
    size_t user : 1;
    size_t page_level_write_through : 1;
    size_t page_level_cache_disable : 1;
    size_t accessed : 1;
    size_t __ignored_0: 1;
    size_t page_size : 1;
    size_t __ignored_1 : 4;
    uintptr_t phys_addr : 51;
    size_t no_execute : 1;
} __packed;


/*
 *  Page Table Entry (Page) Structure
 *  References:
 *      - [1] Table 4-11. Format of a PAE Page-Table Entry that Maps a 4-KByte Page
 */

struct __page {
    size_t present : 1;
    size_t write : 1;
    size_t user : 1;
    size_t page_level_write_through : 1;
    size_t page_level_cache_disable : 1;
    size_t accessed : 1;
    size_t dirty : 1;
    size_t memory_type: 1;
    size_t global : 1;
    size_t __ignored : 3;
    uintptr_t phys_addr : 51;
    size_t no_execute : 1;
} __packed;

#define GET_PHYS_ADDR(structure) (((structure)->phys_addr) << 12)

int paging_pae_check_support()
{
    struct cpu_features;
    get_cpu_features(&cpu_features);
    return cpu_features.pae;
}

void paging_pae_enable()
{

}

//void set_page_table_entry(struct __table table, struct __page page)
//{
//
//}
