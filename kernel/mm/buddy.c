/**********************************************************************
 *                  Buddy Memory Allocator 
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/panic.h>
#include <ds/buddy.h>
#include <ds/bitmap.h>
#include <mm/heap.h>


#define BUDDY_MAX_ORDER (10)
#define BUDDY_MIN_BS (4096)
#define BUDDY_MAX_BS (BUDDY_MIN_BS << BUDDY_MAX_ORDER)

struct buddy buddies[BUDDY_MAX_ORDER+1];

size_t buddy_recursive_alloc(size_t order)
{
    if (order > BUDDY_MAX_ORDER)
        return -1;

    /* Check if there is a free bit in current order */
    if (buddies[order].usable) {
        /* Search for a free bit in current order */
        for (size_t i = buddies[order].first_free_idx; i <= buddies[order].bitmap.max_idx; ++i) {

            /* If bit at i is not checked */
            if (!bitmap_check(&buddies[order].bitmap, i)) {

                /* Mark the bit as used */
                bitmap_set(&buddies[order].bitmap, i);
                --buddies[order].usable;

                /* Shift first_free_idx to search after child_idx */
                buddies[order].first_free_idx = i + 1;

                return i;
            }                
        }

        return -1;
    } else {
        /* Search for a buddy in higher order to split */
        size_t idx = buddy_recursive_alloc(order + 1);

        /* Could not find a free budy */
        if (idx == (size_t) -1) return -1;

        /* Select the left child of the selected bit */
        size_t child_idx = idx << 1;

        /* Mark the selected bit as used */
        bitmap_set(&buddies[order].bitmap, child_idx);

        /* Mark it's buddy as free */
        bitmap_clear(&buddies[order].bitmap, BUDDY_IDX(child_idx));
        ++buddies[order].usable;

        /* Shift first_free_idx to search after child_idx */
        buddies[order].first_free_idx = child_idx + 1;

        return child_idx;
    }

    return -1;
}

void buddy_recursive_free(size_t order, size_t idx)
{
    if (order > BUDDY_MAX_ORDER) return;
    if (idx > buddies[order].bitmap.max_idx) return;

    /* Can't free an already free bit */
    if (!bitmap_check(&buddies[order].bitmap, idx)) return;

    /* Check if buddy bit is free, then combine */
    if (order < BUDDY_MAX_ORDER && !bitmap_check(&buddies[order].bitmap, BUDDY_IDX(idx))) {
        bitmap_set(&buddies[order].bitmap, BUDDY_IDX(idx));
        --buddies[order].usable;

        buddy_recursive_free(order + 1, idx >> 1);
    } else {
        bitmap_clear(&buddies[order].bitmap, idx);
        ++buddies[order].usable;

        /* Update first_free_idx */
        if (buddies[order].first_free_idx > idx)
            buddies[order].first_free_idx = idx;
    }
}

uintptr_t buddy_alloc(size_t _sz)
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

    size_t idx = buddy_recursive_alloc(order);

    if (idx != (size_t) -1)
        return (uintptr_t) (idx * (BUDDY_MIN_BS << order));
    else
        panic("Cannot find free buddy");
        //return (uintptr_t) NULL;
}

void buddy_free(uintptr_t addr, size_t size)
{
    //printk("buddy_free(%x, %d) => ", addr, size);

    size_t sz = BUDDY_MIN_BS;

    /* FIXME */
    size_t order = 0;
    for (; order < BUDDY_MAX_ORDER; ++order) {
        if (sz >= size)
            break;
        sz <<= 1;
    }

    size_t idx = (addr / (BUDDY_MIN_BS << order)) & (sz - 1);

    //printk("order = %d, idx = %d\n", order, idx);

    buddy_recursive_free(order, idx);
}

void buddy_dump()
{
    for (size_t i = 0; i <= BUDDY_MAX_ORDER; ++i) {
        size_t blocks = bitmap_size(buddies[i].bitmap.max_idx + 1)/MEMBER_SIZE(bitmap_t, map[0]);
        printk("Order %d: [%d blocks][%d free bit(s)]\n", i, blocks, buddies[i].usable);
        for (size_t j = 0; j < blocks; ++j) {
            printk("map[%d] = %x\n", j, buddies[i].bitmap.map[j]);
        }
        printk("\n");
    }
}

void buddy_setup(size_t total_mem)
{
    printk("[0] Kernel: PMM -> Setting up Buddy System\n");

    size_t total_size = 0;
    size_t bits_cnt = total_mem / BUDDY_MAX_BS;

    for (int i = BUDDY_MAX_ORDER; i >= 0; --i) {
        size_t bmsize = bitmap_size(bits_cnt);

        buddies[i].bitmap.map = heap_alloc(bmsize, 4);
        buddies[i].bitmap.max_idx = bits_cnt - 1;

        total_size += bmsize;
            
        bits_cnt <<= 1;
    }

    /* Set the heighst order as free and the rest as unusable */
    bitmap_clear_range(&buddies[BUDDY_MAX_ORDER].bitmap, 0, buddies[BUDDY_MAX_ORDER].bitmap.max_idx);
    buddies[BUDDY_MAX_ORDER].first_free_idx = 0;
    buddies[BUDDY_MAX_ORDER].usable = buddies[BUDDY_MAX_ORDER].bitmap.max_idx + 1;

    for (int i = 0; i < BUDDY_MAX_ORDER; ++i) {
        bitmap_set_range(&buddies[i].bitmap, 0, buddies[i].bitmap.max_idx);
        buddies[i].first_free_idx = -1;
        buddies[i].usable = 0;
    }

    /* FIXME */
    size_t kernel_buddies = ((uintptr_t) LMA(kernel_heap) + BUDDY_MAX_BS - 1)/BUDDY_MAX_BS;

    for (size_t i = 0; i < kernel_buddies; ++i) {
        buddy_alloc(BUDDY_MAX_BS);
    }
}
