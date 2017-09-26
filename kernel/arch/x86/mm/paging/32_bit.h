#ifndef _PAGING_32_BIT_H
#define _PAGING_32_BIT_H

#include <core/system.h>

/*
 *  Page Directory (Directory) Structure
 *  References:
 *      - [1] Table 4-3. Use of CR3 with 32-Bit Paging
 */

typedef union __directory {
    struct {
        size_t __ignored_0 : 3;
        size_t page_level_write_through : 1;
        size_t page_level_cache_disable : 1;
        size_t __ignored_1 : 7;
        uintptr_t phys_addr : 20;
    } __packed structure;
    uint32_t raw;
} __packed __directory_t;

/*
 *  Page Directory Entry (Table) Structure
 *  References:
 *      - [1] Table 4-5. Format of a 32-Bit Page-Directory Entry that References a Page Table
 */

typedef union __table {
    struct {
        size_t present : 1;
        size_t write : 1;
        size_t user : 1;
        size_t page_level_write_through : 1;
        size_t page_level_cache_disable : 1;
        size_t accessed : 1;
        size_t __ignored_0: 1;
        size_t page_size : 1;
        size_t __ignored_1 : 4;
        uintptr_t phys_addr : 20;
    } __packed structure;
    uint32_t raw;
} __packed __table_t;


/*
 *  Page Table Entry (Page) Structure
 *  References:
 *      - [1] Table 4-6. Format of a 32-Bit Page-Table Entry that Maps a 4-KByte Page
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
        size_t __ignored : 3;
        uintptr_t phys_addr : 20;
    } __packed structure;
    uint32_t raw;
} __packed __page_t;

#define LOWER_PAGE_BOUNDARY(ptr) ((ptr) & ~PAGE_MASK)
#define UPPER_PAGE_BOUNDARY(ptr) (((ptr) + PAGE_MASK) & ~PAGE_MASK)

#define LOWER_TABLE_BOUNDARY(ptr) ((ptr) & ~TABLE_MASK)
#define UPPER_TABLE_BOUNDARY(ptr) (((ptr) + TABLE_MASK) & ~TABLE_MASK)

#define GET_PHYS_ADDR(s) (((s)->structure.phys_addr) << 12)

typedef union {
    struct {
        size_t offset : 12;
        size_t table : 10;
        size_t directory : 10;
    } __packed structure;
    uint32_t raw;
} __packed __virtaddr_t;

static struct {
    int refs;
} __packed pages[768*1024] = {0};

#endif /* !_PAGING_32_BIT_H */
