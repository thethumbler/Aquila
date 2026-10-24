/** \file
 *                  Buddy Memory Allocator 
 */

#include <core/system.h>
#include <core/panic.h>
#include <ds/bitmap.h>
#include <ds/buddy.h>
#include <mm/buddy.h>
#include <boot/boot.h>

size_t k_total_mem, k_used_mem;
static uintptr_t kstart = 0, kend = 0;

/*
 * What isn't free at boot is set unusable a block of BUDDY_MAX_BS at a
 * time, the ranges kept; the first allocation gives the rest of those
 * blocks back, a page at a time (buddy_reserved_done()).
 */
#define RESERVED_MAX    64

static struct {
    paddr_t start, end;
} reserved[RESERVED_MAX];

static int reserved_nr, reserved_done;

/* the pages of each zone that are RAM (buddy_setup) */
static size_t zone_blocks[BUDDY_ZONE_NR];

/** the bitmaps are sized by the memory, from what mm_setup() set aside */
static inline void *alloc(size_t size, size_t align)
{
    extern void *mm_boot_alloc(size_t size, size_t align);
    return mm_boot_alloc(size, align);
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
static void buddy_reserved_done(void);

/** a block of `_sz' (rounded to a power of two) from `zone', or
 * (paddr_t) -1 if it has none */
paddr_t buddy_alloc(int zone, size_t _sz)
{
    if (_sz > BUDDY_MAX_BS)
        panic("Cannot allocate buddy");

    if (!reserved_done)
        buddy_reserved_done();

    size_t sz = BUDDY_MIN_BS;

    /* FIXME */
    size_t order = 0;
    for (; order <= BUDDY_MAX_ORDER; ++order) {
        if (sz >= _sz)
            break;
        sz <<= 1;
    }

    size_t idx = buddy_recursive_alloc(zone, order);

    if (idx == (size_t) -1)
        return (paddr_t) -1;

    k_used_mem += sz;
    return buddy_zone_offset[zone] + (uintptr_t) (idx * (BUDDY_MIN_BS << order));
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
    if (!size) return;

    printk("buddy: set unusable: %p-%p\n", addr, addr+size-1);

    if (reserved_done)
        panic("buddy: memory set unusable after the first allocation");

    if (reserved_nr == RESERVED_MAX)
        panic("buddy: too many unusable ranges");

    /* its pages, whole */
    reserved[reserved_nr].start = addr & ~(paddr_t) (BUDDY_MIN_BS - 1);
    reserved[reserved_nr].end   = (addr + size + BUDDY_MIN_BS - 1) & ~(paddr_t) (BUDDY_MIN_BS - 1);
    reserved_nr++;

    /* End of region (exclusive), clamped on wrap-around */
    uintptr_t end = addr + size;
    if (end < addr)
        end = (uintptr_t) -1;

    for (int zone = 0; zone < BUDDY_ZONE_NR; ++zone) {

        uintptr_t zone_start = buddy_zone_offset[zone];
        uintptr_t zone_end   = buddy_zone_offset[zone+1];

        if (end <= zone_start || addr >= zone_end)
            continue;

        struct buddy *buddy = &buddies[zone][BUDDY_MAX_ORDER];

        uintptr_t zone_addr = MAX(addr, zone_start) - zone_start;
        uintptr_t zone_size = MIN(end, zone_end) - zone_start - zone_addr;

        /* Indices of first and last (inclusive) blocks touched */
        size_t start_idx = zone_addr / BUDDY_MAX_BS;
        size_t end_idx   = (zone_addr + zone_size - 1) / BUDDY_MAX_BS;

        /* Region lies beyond available memory in this zone */
        if (start_idx > buddy->bitmap.max_idx)
            continue;

        if (end_idx > buddy->bitmap.max_idx)
            end_idx = buddy->bitmap.max_idx;

        for (size_t idx = start_idx; idx <= end_idx; ++idx) {
            /* Block may already be marked by an overlapping region */
            if (!bitmap_check(&buddy->bitmap, idx)) {
                bitmap_set(&buddy->bitmap, idx);
                buddy->usable--;
            }
        }

        size_t ffidx = buddy->first_free_idx;

        if (ffidx >= start_idx && ffidx <= end_idx)
            buddy->first_free_idx = end_idx + 1;
    }
}

static int reserved_page(paddr_t page)
{
    for (int i = 0; i < reserved_nr; ++i)
        if (page >= reserved[i].start && page < reserved[i].end)
            return 1;

    return 0;
}

/**
 * The blocks set unusable at boot: their pages that no range covers are
 * freed (and merge back into bigger blocks as they would), the others stay
 * used. What's free then is RAM less what's reserved, to the page.
 */
static void buddy_reserved_done(void)
{
    size_t free = 0;

    reserved_done = 1;

    for (int zone = 0; zone < BUDDY_ZONE_NR; ++zone) {
        struct buddy *top = &buddies[zone][BUDDY_MAX_ORDER];

        for (size_t idx = 0; idx < zone_blocks[zone]; ++idx) {
            if (!bitmap_check(&top->bitmap, idx)) {
                free += BUDDY_MAX_BS;
                continue;
            }

            paddr_t block = buddy_zone_offset[zone] + idx * BUDDY_MAX_BS;
            size_t first = idx << BUDDY_MAX_ORDER;

            for (size_t i = 0; i < (1U << BUDDY_MAX_ORDER); ++i) {
                if (!reserved_page(block + i * BUDDY_MIN_BS)) {
                    buddy_recursive_free(zone, 0, first + i);
                    free += BUDDY_MIN_BS;
                }
            }
        }
    }

    /* what /proc/meminfo's MemFree is: k_total_mem - k_used_mem */
    k_used_mem = k_total_mem > free ? k_total_mem - free : 0;
}

int buddy_setup(size_t total_mem)
{
    printk("buddy: Setting up buddy allocator (total memory %p)\n", total_mem);

    k_total_mem = total_mem;
    k_used_mem  = 0;

    for (int zone = 0; zone < BUDDY_ZONE_NR; ++zone) {
        size_t bits_cnt = 0;

        uintptr_t zone_start = buddy_zone_offset[zone];
        uintptr_t zone_end   = MIN(buddy_zone_offset[zone+1], total_mem);

        if (zone_end > zone_start)
            bits_cnt = (zone_end - zone_start) / BUDDY_MAX_BS;

        zone_blocks[zone] = bits_cnt;

        if (!bits_cnt) {
            /* No memory in this zone, keep a single block marked as used */
            for (int i = BUDDY_MAX_ORDER; i >= 0; --i) {
                buddies[zone][i].bitmap.map = alloc(bitmap_size(1), 4);
                buddies[zone][i].bitmap.max_idx = 0;
                bitmap_set(&buddies[zone][i].bitmap, 0);
                buddies[zone][i].first_free_idx = -1;
                buddies[zone][i].usable = 0;
            }

            continue;
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
        elf_shdr_t *shdr = __kboot->symtab;
        buddy_set_unusable(LMA((uintptr_t) shdr->sh_addr), shdr->sh_size);
    }

    if (__kboot->strtab) {
        elf_shdr_t *shdr = __kboot->strtab;
        buddy_set_unusable(LMA((uintptr_t) shdr->sh_addr), shdr->sh_size);
    }

    return 0;
}
