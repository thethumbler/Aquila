/**********************************************************************
 *					Physical Memory Manager
 *
 *
 *	This file is part of Aquila OS and is released under the terms of
 *	GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/string.h>
#include <core/panic.h>
#include <cpu/cpu.h>
#include <boot/multiboot.h>
#include <boot/boot.h>
#include <mm/mm.h>
#include <ds/bitmap.h>

static char *kheap = NULL;

static inline void *heap_alloc(size_t size, size_t align)
{
	char *ret = (char *)((uintptr_t)(kheap + align - 1) & (~(align - 1)));
	kheap = ret + size;

	memset(ret, 0, size);	/* We always clear the allocated area */

	return ret;
}

/* Physical Memory Bitmap */
static volatile bitmap_t pm_bitmap = NULL;
static uint32_t bitmap_max_index = 0;

static volatile uint32_t *BSP_PD = NULL;

#define P  _BV(0)
#define RW _BV(1)

void pmm_setup(struct boot *boot)
{
	printk("Total memory: %d KiB, %d MiB\n", boot->total_mem, boot->total_mem / 1024);

	extern char *kernel_heap;
	kheap = VMA(kernel_heap);

	printk("kheap=%x\n", kheap);

	unsigned long buddies = boot->total_mem / 4096;
	printk("#4 MiB Buddies=%dB\n", BITMAP_SIZE(buddies));
	/*int size = 4096;
	int large = boot->total_mem / size;
	int total_buddies = large;

	for (; size >= 4; size /= 2) {
		printk("# %d KiB Buddies: %d\n", size, large *= 2);
		total_buddies += large;
	}

	printk("Total buddies=%d [%d B]\n", total_buddies, total_buddies / 32);
	*/
	struct cpu_features features = get_cpu_features();

	/* We can have either 4 KiB or 4 MiB pages -- We don't support PAE */
	if (features.pse)
		printk("PSE=%d\n", features.pse);

	if (features.pge)
		printk("PGE=%d\n", features.pge);

	for (;;);

	bitmap_max_index = boot->total_mem / PAGE_SIZE;
	
	for (int i = 0; i < boot->mmap_count; ++i) {
		BITMAP_SET_RANGE(pm_bitmap,
							(boot->mmap[i].start + PAGE_MASK) / PAGE_SIZE,
							boot->mmap[i].end / PAGE_SIZE);
	}

	extern char kernel_end;
	BITMAP_CLR_RANGE(pm_bitmap, 0, (uintptr_t)&kernel_end/PAGE_SIZE);

	/* Modules */
	for (int i = 0; i < boot->modules_count; ++i) {
		BITMAP_CLR_RANGE(pm_bitmap,
					(LMA((uintptr_t)boot->modules[i].addr) + PAGE_MASK) / PAGE_SIZE,
					(LMA((uintptr_t)boot->modules[i].addr + boot->modules[i].size) + PAGE_MASK) / PAGE_SIZE);
	}

	/* Fix pointers to BSP_PD and BSP_LPT */
	extern volatile uint32_t *_BSP_PD;

	BSP_PD = VMA(_BSP_PD);
}

static void *mount_page(uintptr_t paddr)
{
	BSP_PD[1023] = (paddr & (~PAGE_MASK)) | P | RW;
	asm("invlpg (%%eax)"::"a"(~PAGE_MASK));
	for(;;);
	return (void*)(~PAGE_MASK); /* Last page */
}

static uint32_t last_checked_index = 0;
static uintptr_t get_frame()
{
	unsigned i;
	for (i = last_checked_index; i < bitmap_max_index; ++i)
		if (BITMAP_CHK(pm_bitmap, i)) {
			last_checked_index = i;
			void *p = mount_page(i * PAGE_SIZE);
			memset(p, 0, PAGE_SIZE);
			return BITMAP_CLR(pm_bitmap, i), i * PAGE_SIZE;
		}
	return -1;
}

void release_frame(uintptr_t i)
{
	BITMAP_CLR(pm_bitmap, i/PAGE_SIZE);
}

uintptr_t get_frame_no_clr()
{
	unsigned i;
	for(i = last_checked_index; i < bitmap_max_index; ++i)
		if(BITMAP_CHK(pm_bitmap, i))
		{
			last_checked_index = i;
			return BITMAP_CLR(pm_bitmap, i), i * PAGE_SIZE;
		}
	return -1;
}

static uintptr_t get_current_page_directory()
{
	uintptr_t cur_pd = 0;
	asm("movl %%cr3, %%eax":"=a"(cur_pd));
	return cur_pd;
}

static int map_to_physical(uintptr_t ptr, size_t size, int flags)
{
	flags =  0x1 | ((flags & (KW | UW)) ? 0x2 : 0x0)
		| ((flags & URWX) ? 0x4 : 0x0);

	uint32_t *cur_pd_phys = (uint32_t *) get_current_page_directory();
	uint32_t cur_pd[PAGE_SIZE/4];
	pmman.memcpypv(cur_pd, cur_pd_phys, PAGE_SIZE);

	/* Number of tables needed for mapping */
	uint32_t tables = (ptr + size + TABLE_MASK)/TABLE_SIZE - ptr/TABLE_SIZE;

	/* Number of pages needed for mapping */
	uint32_t pages  = (ptr + size + PAGE_MASK)/PAGE_SIZE - ptr/PAGE_SIZE;

	/* Index of the first table in the Page Directory */
	uint32_t tindex  = ptr/TABLE_SIZE;

	/* Index of the first page in the first table */
	uint32_t pindex = (ptr & TABLE_MASK)/PAGE_SIZE;

	uint32_t i;
	for(i = 0; i < tables; ++i)
	{
		/* If table is not allocated already, get one
		   note that the frame we just got is already mounted */
		if(!(cur_pd[tindex + i] &1))
			cur_pd[tindex + i] = get_frame() | flags;
		else /* Otherwise, just mount the already allocated table */
			mount_page(cur_pd[tindex + i] & ~PAGE_MASK);

		/* Mounted pages are mapped to the last page of 4GB system */
		uint32_t *PT = (uint32_t*) ~PAGE_MASK; /* Last page */

		while(pages--)	/* Repeat as long as we still have pages to allocate */
		{
			/* Allocate page if it is not already allocated
			   Note that get_frame_no_clr() does not mount the page */
			if(!(PT[pindex] & 1))
				PT[pindex] = get_frame_no_clr() | flags;

			++pindex;
			if(pindex == 1024) break; /* Get out once the table is filled */
		}

		pindex = 0;	/* Now we are pointing to a new table so we clear pindex */
	}

	pmman.memcpyvp(cur_pd_phys, cur_pd, PAGE_SIZE);

	return 1;
}

static void unmap_from_physical(uintptr_t ptr, size_t size)
{
	uint32_t *cur_pd_phys = (uint32_t *) get_current_page_directory();
	uint32_t cur_pd[PAGE_SIZE/4];
	pmman.memcpypv(cur_pd, cur_pd_phys, PAGE_SIZE);

	/* Number of tables that are mapped, we unmap only on table boundary */
	uint32_t tables = (ptr + size + TABLE_MASK)/TABLE_SIZE - ptr/TABLE_SIZE;

	/* Number of pages to be unmaped */
	uint32_t pages  = (ptr + size)/PAGE_SIZE - (ptr + PAGE_MASK)/PAGE_SIZE;

	/* Index of the first table in the Page Directory */
	uint32_t tindex = ptr/TABLE_SIZE;

	/* Index of the first page in the first table */
	uint32_t pindex = ((ptr & TABLE_MASK) + PAGE_MASK)/PAGE_SIZE;

	/* We first check if our pindex, is the first page of the first table */
	if(pindex)	/* pindex does not point to the first page of the first table */
	{
		/* We proceed if the table is already mapped, otherwise, skip it */
		if(cur_pd[tindex] & 1)
		{
			/* First, we mount the table */
			uint32_t *PT = mount_page(cur_pd[tindex]);

			/* We unmap pages only, the table is not unmapped */
			while(pages && (pindex < 1024))
			{
				BITMAP_CLR(pm_bitmap, PT[pindex] >> PAGE_SHIFT);
				PT[pindex] = 0;
				++pindex;
				--pages;
			}
		}

		/*We point to the next table, decrement tables count and reset pindex */
		--tables;
		++tindex;
		pindex = 0;
	}

	/* Now we iterate over all remaining tables */
	while(tables--)
	{
		/* unmapping already mapped tables only, othewise skip */
		if(cur_pd[tindex] & 1)
		{
			/* Mount the table so we can modify it */
			uint32_t *PT = mount_page(cur_pd[tindex] & ~PAGE_MASK);

			/* iterate over pages, stop if we reach the final page or the number
			   of pages left to unmap is zero */
			while((pindex < 1024) && pages)
			{
				BITMAP_CLR(pm_bitmap, PT[pindex] >> PAGE_SHIFT);
				PT[pindex] = 0;
				++pindex;
				--pages;
			}

			if(pindex == 1024) /* unamp table only if we reach it's last page */
			{
				BITMAP_CLR(pm_bitmap, cur_pd[tindex] >> PAGE_SHIFT);
				cur_pd[tindex] = 0;
			}

			/* reset pindex */
			pindex = 0;
		}

		++tindex;
	}

	pmman.memcpyvp(cur_pd_phys, cur_pd, PAGE_SIZE);
}

uintptr_t arch_get_frame()
{
	return get_frame();
}

uintptr_t arch_get_frame_no_clr()
{
	return get_frame_no_clr();
}

void arch_release_frame(uintptr_t p)
{
	release_frame(p);
}

static void *memcpypv(void *_virt_dest, void *_phys_src, size_t n)
{
	char *virt_dest = (char *) _virt_dest;
	char *phys_src  = (char *) _phys_src;

	void *ret = virt_dest;

	/* Copy up to page boundary */
	size_t offset = (uintptr_t) phys_src % PAGE_SIZE;
	size_t size = MIN(n, PAGE_SIZE - offset);
	
	if(size)
	{
		char *p = mount_page((uintptr_t) phys_src);
		memcpy(virt_dest, p + offset, size);
		
		phys_src  += size;
		virt_dest += size;

		/* Copy complete pages */
		n -= size;
		size = n / PAGE_SIZE;
		while(size--)
		{
			char *p = mount_page((uintptr_t) phys_src);
			memcpy(virt_dest, p, PAGE_SIZE);
			phys_src += PAGE_SIZE;
			virt_dest += PAGE_SIZE;
		}

		/* Copy what is remainig */
		size = n % PAGE_SIZE;
		if(size)
		{
			p = mount_page((uintptr_t) phys_src);
			memcpy(virt_dest, p, size);
		}
	}
	return ret;
}

static void *memcpyvp(void *_phys_dest, void *_virt_src, size_t n)
{
	char *phys_dest = (char *) _phys_dest;
	char *virt_src  = (char *) _virt_src;
	void *ret = phys_dest;

	size_t s = n / PAGE_SIZE;
	while(s--)
	{
		void *p = mount_page((uintptr_t) phys_dest);
		memcpy(p, virt_src, PAGE_SIZE);
		phys_dest += PAGE_SIZE;
		virt_src  += PAGE_SIZE;
	}

	s = n % PAGE_SIZE;

	void *p = mount_page((uintptr_t) phys_dest);
	memcpy(p, virt_src, s);
	return ret;
}

static char memcpy_pp_buf[PAGE_SIZE];
static void *memcpypp(void *_phys_dest, void *_phys_src, size_t n)
{
	/* XXX: This function is catastrophic */
	char *phys_dest = (char *) _phys_dest;
	char *phys_src  = (char *) _phys_src;
	void *ret = phys_dest;

	/* Copy up to page boundary */
	size_t offset = (uintptr_t) phys_src % PAGE_SIZE;
	size_t size = MIN(n, PAGE_SIZE - offset);

	char *p = mount_page((uintptr_t) phys_src);
	memcpy(memcpy_pp_buf + offset, p + offset, size);
	p = mount_page((uintptr_t) phys_dest);
	memcpy(p + offset, memcpy_pp_buf + offset, size);
	
	phys_src  += size;
	phys_dest += size;
	n -= size;

	/* Copy complete pages */
	size = n / PAGE_SIZE;
	while(size--)
	{
		p = mount_page((uintptr_t) phys_src);
		memcpy(memcpy_pp_buf, p, PAGE_SIZE);
		p = mount_page((uintptr_t) phys_dest);
		memcpy(p, memcpy_pp_buf, PAGE_SIZE);
		phys_src += PAGE_SIZE;
		phys_dest += PAGE_SIZE;
	}

	/* Copy what is remainig */
	size = n % PAGE_SIZE;
	p = mount_page((uintptr_t) phys_src);
	memcpy(memcpy_pp_buf, p, size);
	p = mount_page((uintptr_t) phys_dest);
	memcpy(p, memcpy_pp_buf, size);

	return ret;
}

pmman_t pmman = (pmman_t)
{
	.map = &map_to_physical,
	.unmap = &unmap_from_physical,
	.memcpypv = &memcpypv,
	.memcpyvp = &memcpyvp,
	.memcpypp = &memcpypp,
};