/*
 *                  Buddy Memory Allocator 
 *
 *
 *  This file is part of AquilaOS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */

#include <core/system.h>
#include <core/panic.h>
#include <ds/bitmap.h>
#include <ds/buddy.h>
#include <mm/buddy.h>
#include <boot/boot.h>

size_t k_total_mem, k_used_mem;
static uintptr_t kstart = 0, kend = 0;

static char alloc_area[1024 * 1024]; /* 1 MiB heap area */
static char *alloc_mark = alloc_area;

static inline void *alloc(size_t size, size_t align)
{
    char *ret = (char *)((uintptr_t)(alloc_mark + align - 1) & (~(align - 1)));
    alloc_mark = ret + size;
    memset(ret, 0, size);
    return ret;
}

struct buddy buddies[BUDDY_ZONE_NR][BUDDY_MAX_ORDER+1];
uintptr_t buddy_zone_offset[BUDDY_ZONE_NR+1] = {
    [BUDDY_ZONE_DMA]     = 0,           /* 0 - 16 MiB */
    [BUDDY_ZONE_NORMAL]  = 0x1000000,   /* 16 MiB -  */
    [BUDDY_ZONE_NR]      = (uintptr_t) -1,
};

static size_t buddy_recursive_alloc(int zone, size_t order)
{
    if (order > BUDDY_MAX_ORDER)
        return -1;

    /* Check if there is a free bit in current order */
    if (buddies[zone][order].usable) {
        /* Search for a free bit in current order */
        for (size_t i = buddies[zone][order].first_free_idx; i <= buddies[zone][order].bitmap.max_idx; ++i) {

            /* If bit at i is not checked */
            if (!bitmap_check(&buddies[zone][order].bitmap, i)) {

                /* Mark the bit as used */
                bitmap_set(&buddies[zone][order].bitmap, i);
                buddies[zone][order].usable--;

                /* Shift first_free_idx to search after child_idx */
                buddies[zone][order].first_free_idx = i + 1;

                return i;
            }                
        }

        return -1;
    } else {
        /* Search for a buddy in higher order to split */
        size_t idx = buddy_recursive_alloc(zone, order + 1);

        /* Could not find a free budy */
        if (idx == (size_t) -1) return -1;

        /* Select the left child of the selected bit */
        size_t child_idx = idx << 1;

        /* Mark the selected bit as used */
        bitmap_set(&buddies[zone][order].bitmap, child_idx);

        /* Mark it's buddy as free */
        bitmap_clear(&buddies[zone][order].bitmap, BUDDY_IDX(child_idx));
        buddies[zone][order].usable++;

        /* Shift first_free_idx to search after child_idx */
        buddies[zone][order].first_free_idx = child_idx + 1;

        return child_idx;
    }
}

static void buddy_recursive_free(int zone, size_t order, size_t idx)
{
    if (order > BUDDY_MAX_ORDER) return;
    if (idx > buddies[zone][order].bitmap.max_idx) return;

    /* Can't free an already free bit */
    if (!bitmap_check(&buddies[zone][order].bitmap, idx)) return;

    /* Check if buddy bit is free, then combine */
    if (order < BUDDY_MAX_ORDER && !bitmap_check(&buddies[zone][order].bitmap, BUDDY_IDX(idx))) {
        bitmap_set(&buddies[zone][order].bitmap, BUDDY_IDX(idx));
        buddies[zone][order].usable--;

        buddy_recursive_free(zone, order + 1, idx >> 1);
    } else {
        bitmap_clear(&buddies[zone][order].bitmap, idx);
        buddies[zone][order].usable++;

        /* Update first_free_idx */
        if (buddies[zone][order].first_free_idx > idx)
            buddies[zone][order].first_free_idx = idx;
    }
}

/** allocate new buddy
 * @param zone zone index
 * @param _sz chunk size
 */
paddr_t buddy_alloc(int zone, size_t _sz)
{
    if (_sz > BUDDY_MAX_BS)
        panic("Cannot allocate buddy");

    size_t sz = BUDDY_MIN_BS;

    /* FIXME */
    size_t order = 0;
    for (; order <= BUDDY_MAX_ORDER; ++order) {
        if (sz >= _sz)
            break;
        sz <<= 1;
    }

    k_used_mem += sz;

    size_t idx = buddy_recursive_alloc(zone, order);

    if (idx != (size_t) -1) {
        return buddy_zone_offset[zone] + (uintptr_t) (idx * (BUDDY_MIN_BS << order));
    } else {
        panic("Cannot find free buddy");
        //return (uintptr_t) NULL;
    }
}

void buddy_free(int zone, paddr_t addr, size_t size)
{
    if (addr >= kstart && addr < kend)
        panic("trying to free from kernel code");

    size_t sz = BUDDY_MIN_BS;

    /* FIXME */
    size_t order = 0;
    for (; order < BUDDY_MAX_ORDER; ++order) {
        if (sz >= size)
            break;
        sz <<= 1;
    }

    k_used_mem -= sz;

    addr -= buddy_zone_offset[zone];
    size_t idx = (addr / (BUDDY_MIN_BS << order));  // & (sz - 1);
    buddy_recursive_free(zone, order, idx);
}

void buddy_set_unusable(paddr_t addr, size_t size)
{

    printk("buddy: set unusable: %p-%p\n", addr, addr+size-1);

    for (int zone = 0; size && zone < BUDDY_ZONE_NR; ++zone) {

        uintptr_t zone_start = buddy_zone_offset[zone];
        uintptr_t zone_end   = buddy_zone_offset[zone+1];

        if (addr + size < zone_start || addr >= zone_end)
            continue;

        uintptr_t zone_addr = MAX(addr, zone_start) - zone_start;
        uintptr_t zone_size = MIN(addr + size, zone_end) - zone_start - zone_addr;

        size_t start_idx = zone_addr / BUDDY_MAX_BS;
        size_t end_idx   = (zone_addr + zone_size + BUDDY_MAX_BS - 1) / BUDDY_MAX_BS;

        if (end_idx > buddies[zone][BUDDY_MAX_ORDER].bitmap.max_idx)
            end_idx = buddies[zone][BUDDY_MAX_ORDER].bitmap.max_idx;

        bitmap_set_range(&buddies[zone][BUDDY_MAX_ORDER].bitmap, start_idx, end_idx);

        size_t ffidx = buddies[zone][BUDDY_MAX_ORDER].first_free_idx;

        if (ffidx >= start_idx && ffidx <= end_idx)
            buddies[zone][BUDDY_MAX_ORDER].first_free_idx = end_idx + 1;

        buddies[zone][BUDDY_MAX_ORDER].usable -= (end_idx - start_idx + 1);

        k_used_mem += size;
    }
}

int buddy_setup(size_t total_mem)
{
    printk("buddy: Setting up buddy allocator (total memory %p)\n", total_mem);

    k_total_mem = total_mem;
    k_used_mem  = 0;

    for (int zone = 0; zone < BUDDY_ZONE_NR; ++zone) {
        size_t bits_cnt = 0;

        if (zone < BUDDY_ZONE_NR - 1) {
            bits_cnt = (buddy_zone_offset[zone+1] - buddy_zone_offset[zone]) / BUDDY_MAX_BS;
        } else {
            /* Last zone */
            bits_cnt = (total_mem - buddy_zone_offset[zone]) / BUDDY_MAX_BS;
        }

        for (int i = BUDDY_MAX_ORDER; i >= 0; --i) {
            size_t bmsize = bitmap_size(bits_cnt);

            buddies[zone][i].bitmap.map = alloc(bmsize, 4);
            buddies[zone][i].bitmap.max_idx = bits_cnt - 1;

            bits_cnt <<= 1;
        }

        /* Set the heighst order as free and the rest as unusable */
        bitmap_clear_range(&buddies[zone][BUDDY_MAX_ORDER].bitmap, 0, buddies[zone][BUDDY_MAX_ORDER].bitmap.max_idx);
        buddies[zone][BUDDY_MAX_ORDER].first_free_idx = 0;
        buddies[zone][BUDDY_MAX_ORDER].usable = buddies[zone][BUDDY_MAX_ORDER].bitmap.max_idx + 1;

        for (int i = 0; i < BUDDY_MAX_ORDER; ++i) {
            bitmap_set_range(&buddies[zone][i].bitmap, 0, buddies[zone][i].bitmap.max_idx);
            buddies[zone][i].first_free_idx = -1;
            buddies[zone][i].usable = 0;
        }
    }

    /* FIXME */
    extern char kernel_start;
    extern char kernel_end;

    kstart = (uintptr_t) &kernel_start;
    kend   = (uintptr_t) &kernel_end;

    buddy_set_unusable(kstart, kend - kstart);

    extern struct boot *__kboot;

    if (__kboot->symtab) {
        struct elf32_shdr *shdr = __kboot->symtab;
        buddy_set_unusable(LMA((uintptr_t) shdr->sh_addr), shdr->sh_size);
    }

    if (__kboot->strtab) {
        struct elf32_shdr *shdr = __kboot->strtab;
        buddy_set_unusable(LMA((uintptr_t) shdr->sh_addr), shdr->sh_size);
    }

    return 0;
}
