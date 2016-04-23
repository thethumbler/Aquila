#include <core/system.h>
#include <mm/mm.h>
#include <core/string.h>
#include <core/panic.h>
#include <boot/multiboot.h>
#include <ds/bitmap.h>

void *heap;
static void *heap_alloc(size_t size, size_t align)
{
	void *ret = (void*)((uintptr_t)(heap + align - 1) & (~(align - 1)));
	heap = (void*)((uintptr_t)ret + size);

	memset(ret, 0, size);	/* We always clear the allocated area */

	return ret;
}

/* Physical Memory Bitmap */
static volatile bitmap_t pm_bitmap = NULL;
static uint32_t bitmap_max_index = 0;

/* FIXME: make this function bootloader independent */
void x86_mm_setup()
{
	extern void *kernel_heap;
	void **_heap = VMA(&kernel_heap);
	heap = VMA(*_heap);

	/* FIXME: Use bigger type, or make mem size in KBs to avoid overflow
	 * on systems having >= 4GB of RAM
	 */
	uint32_t mem = 0;

	uint32_t _mmap = multiboot_info->mmap_addr;
	multiboot_mmap_t *mmap = (multiboot_mmap_t*)_mmap;
	uint32_t mmap_end = _mmap + multiboot_info->mmap_length;

	while(_mmap < mmap_end)
	{
		mem   += mmap->len;
		_mmap += mmap->size + sizeof(uint32_t);
		mmap   = (multiboot_mmap_t*) _mmap;
	}

	bitmap_max_index = mem / PAGE_SIZE;

	pm_bitmap = heap_alloc(BITMAP_SIZE(bitmap_max_index), 4);

	BITMAP_CLR_RANGE(pm_bitmap, 0, bitmap_max_index);

	_mmap = multiboot_info->mmap_addr;
	_mmap = _mmap;
	mmap = (multiboot_mmap_t*)_mmap;

	while(_mmap < mmap_end)
	{
		if(mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
			BITMAP_SET_RANGE(pm_bitmap,
							(mmap->addr + PAGE_MASK) / PAGE_SIZE,
							(mmap->addr + mmap->len) / PAGE_SIZE);
		_mmap += mmap->size + sizeof(uint32_t);
		mmap   = (multiboot_mmap_t*) _mmap;
	}

	extern char kernel_end;
	BITMAP_CLR_RANGE(pm_bitmap, 0, (uintptr_t)&kernel_end/PAGE_SIZE);

	/* Ramdisk */
	multiboot_module_t *ramdisk = (void *) multiboot_info->mods_addr;
	BITMAP_CLR_RANGE(pm_bitmap,
					(ramdisk->mod_start + PAGE_MASK) / PAGE_SIZE,
					(ramdisk->mod_end   + PAGE_MASK) / PAGE_SIZE);
}

extern uint32_t* BSP_LPT;
void *mount_page(uintptr_t paddr)
{
	*(BSP_LPT + 1023) = (paddr & (~PAGE_MASK)) | 3;
	asm("invlpg (%%eax)"::"a"(~PAGE_MASK));
	return (void*)(~PAGE_MASK); /* Last page */
}


unsigned last_checked_index = 0;
uintptr_t get_frame()
{
	unsigned i;
	for(i = last_checked_index; i < bitmap_max_index; ++i)
		if(BITMAP_CHK(pm_bitmap, i))
		{
			last_checked_index = i;
			void *p = mount_page(i * PAGE_SIZE);
			memset(p, 0, PAGE_SIZE);
			return BITMAP_CLR(pm_bitmap, i), i * PAGE_SIZE;
		}
	return -1;
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

int map_to_physical(uintptr_t ptr, size_t size, int flags)
{
	flags =  0x1 | ((flags & (KW | UW)) ? 0x2 : 0x0)
		| ((flags & URWX) ? 0x4 : 0x0);

	/* Number of tables needed for mapping */
	uint32_t tables = (ptr + size + TABLE_MASK)/TABLE_SIZE - ptr/TABLE_SIZE;

	/* Number of pages needed for mapping */
	uint32_t pages  = (ptr + size + PAGE_MASK)/PAGE_SIZE - ptr/PAGE_SIZE;

	/* Index of the first table in the Page Directory */
	uint32_t tindex  = ptr/TABLE_SIZE;

	/* Index of the first page in the first table */
	uint32_t pindex = (ptr & TABLE_MASK)/PAGE_SIZE;

	extern uint32_t *BSP_PD;

	uint32_t i;
	for(i = 0; i < tables; ++i)
	{
		/* If table is not allocated already, get one
		   note that the frame we just got is already mounted */
		if(!(BSP_PD[tindex + i] &1))
			BSP_PD[tindex + i] = get_frame() | flags;
		else /* Otherwise, just mount the already allocated table */
			mount_page(BSP_PD[tindex + i] & ~PAGE_MASK);

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

	return 1;
}

void unmap_from_physical(uintptr_t ptr, size_t size)
{
	/* Number of tables that are mapped, we unmap only on table boundary */
	uint32_t tables = (ptr + size + TABLE_MASK)/TABLE_SIZE - ptr/TABLE_SIZE;

	/* Number of pages to be unmaped */
	uint32_t pages  = (ptr + size)/PAGE_SIZE - (ptr + PAGE_MASK)/PAGE_SIZE;

	/* Index of the first table in the Page Directory */
	uint32_t tindex = ptr/TABLE_SIZE;

	/* Index of the first page in the first table */
	uint32_t pindex = ((ptr & TABLE_MASK) + PAGE_MASK)/PAGE_SIZE;

	extern uint32_t *BSP_PD;

	/* We first check if our pindex, is the first page of the first table */
	if(pindex)	/* pindex does not point to the first page of the first table */
	{
		/* We proceed if the table is already mapped, otherwise, skip it */
		if(BSP_PD[tindex] & 1)
		{
			/* First, we mount the table */
			uint32_t *PT = mount_page(BSP_PD[tindex] & ~PAGE_MASK);

			/* We unmap pages only, the table is not unmapped */
			while(pages && pindex < 1024)
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
		if(BSP_PD[tindex] & 1)
		{
			/* Mount the table so we can modify it */
			uint32_t *PT = mount_page(BSP_PD[tindex] & ~PAGE_MASK);

			/* iterate over pages, stop if we reach the final page or the number
			   of pages left to unmap is zero */
			while(pindex < 1024 && pages)
			{
				BITMAP_CLR(pm_bitmap, PT[pindex] >> PAGE_SHIFT);
				PT[pindex] = 0;
				++pindex;
				--pages;
			}

			if(pindex == 1024) /* unamp table only if we reach it's last page */
			{
				BITMAP_CLR(pm_bitmap, BSP_PD[tindex] >> PAGE_SHIFT);
				BSP_PD[tindex] = 0;
			}

			/* reset pindex */
			pindex = 0;
		}

		++tindex;
	}
}

pmman_t pmman = (pmman_t)
{
	.map = map_to_physical,
	.unmap = unmap_from_physical
};

typedef struct
{
	uint32_t addr : 26;	/* Offseting (256MiB), 4-bytes aligned objects */
	uint32_t free : 1;	/* Free or not flag */
	uint32_t size : 24;	/* Size of one object can be up to 64MiB */
	uint32_t next : 13; /* Index of the next node */
}__attribute__((packed)) vmm_node_t;


#define VMM_NODES		(0xCFFF0000)
#define VMM_NODES_SIZE	(0x00010000)
#define VMM_BASE		(VMM_NODES + VMM_NODES_SIZE)
#define NODE_ADDR(addr)	(VMM_BASE + (addr) * 4)
#define NODE_SIZE(size)	((size) * 4)
#define LAST_NODE_INDEX	((1 << 13) - 1)
#define MAX_NODE_SIZE	((1 << 24) - 1)

vmm_node_t *nodes = (vmm_node_t *) VMM_NODES;
void x86_vmm_setup()
{
	/* We start by mapping the space used for nodes into physical memory */
	map_to_physical(VMM_NODES, VMM_NODES_SIZE, KRW);

	/* Now we have to clear it */
	memset((void*)VMM_NODES, 0, VMM_NODES_SIZE);

	/* Setting up initial nodes */
	nodes[0] = (vmm_node_t){0x000000, 1, -1, -1};
	/*nodes[1] = (vmm_node_t){0x400000, 1, -1, 2};
	nodes[2] = (vmm_node_t){0x800000, 1, -1, 3};
	nodes[3] = (vmm_node_t){0xC00000, 1, -1, -1};*/
}

uint32_t last_checked_node = 0;
uint32_t get_node()
{
	unsigned i;
	for(i = last_checked_node; i < LAST_NODE_INDEX; ++i)
		if(!nodes[i].size)
			return last_checked_node = i;
	panic("Can't find an unused node");
	return -1;
}

void release_node(uint32_t i)
{
	nodes[i] = (vmm_node_t){0};
	if(i < last_checked_node)
		last_checked_node = i;
}

uint32_t get_first_fit_free_node(uint32_t size)
{
	unsigned i = last_checked_node;
	while(!(nodes[i].free && nodes[i].size >= size))
	{
		if(nodes[i].next == LAST_NODE_INDEX)
			panic("Can't find a free node");
		i = nodes[i].next;
	}

	return i;
}

void print_node(unsigned i)
{
	printk("Node[%d]\n", i);
	printk("   |_ Addr   : %x\n", NODE_ADDR(nodes[i].addr));
	printk("   |_ free ? : %s\n", nodes[i].free?"yes":"no");
	printk("   |_ Size   : %d B [ %d KiB ]\n",
		NODE_SIZE(nodes[i].size), NODE_SIZE(nodes[i].size)/1024 );
	printk("   |_ Next   : %d\n", nodes[i].next );
}

void *kmalloc(size_t size)
{
	size = (size + 3)/4;	/* Round size up to 4 bytes */

	/* Look for a first fit free node */
	unsigned i = get_first_fit_free_node(size);

	/* Mark it as used */
	nodes[i].free = 0;

	/* Split the node if necessary */
	if(nodes[i].size > size)
	{
		unsigned n = get_node();
		nodes[n] = (vmm_node_t)
			{
				.addr = nodes[i].addr + size,
				.free = 1,
				.size = nodes[i].size - size,
				.next = nodes[i].next
			};

		nodes[i].next = n;
		nodes[i].size = size;
	}

	pmman.map(NODE_ADDR(nodes[i].addr), NODE_SIZE(size), KRW);
	return (void*)NODE_ADDR(nodes[i].addr);
}

void kfree(void *_ptr)
{
	if((uintptr_t)_ptr < VMM_BASE)	/* That's not even allocatable */
		return;

	/* Look for the node containing _ptr */
	/* NOTE: We don't use a function for looking for the node, since we also
	   need to know the first free node of a sequence of free nodes before our
	   target node */
	uintptr_t ptr = ((uintptr_t)_ptr - VMM_BASE)/4;

	unsigned i = 0, j = -1;

	while(!(ptr >= nodes[i].addr
		&& ptr < (uintptr_t)(nodes[i].addr + nodes[i].size)))
	{
		if(nodes[i].free && j == -1U) j = i; /* Set first free node index */
		if(!nodes[i].free) j = -1U;	/* Invalidate first free node index */

		i = nodes[i].next;
		if(i == LAST_NODE_INDEX) /* Trying to free unallocated node */
			return;
	}

	/* First we mark our node as free */
	nodes[i].free = 1;

	/* Now we merge all free nodes ahead */
	unsigned n = nodes[i].next;
	while(n < LAST_NODE_INDEX && nodes[n].free)
	{
		/* Hello babe, we need to merge */
		if((uintptr_t)(nodes[n].size + nodes[i].size) <= MAX_NODE_SIZE)
		{
			nodes[i].size += nodes[n].size;
			nodes[i].next  = nodes[n].next;
			release_node(n);
		}
		n = nodes[i].next;
	}

	if(j != i && j != -1U)	/* I must be lucky, aren't I always ;) */
		kfree((void*)NODE_ADDR(nodes[j].addr));
	else
		unmap_from_physical(NODE_ADDR(nodes[i].addr), NODE_SIZE(nodes[i].size));
}

void dump_nodes()
{
	printk("Nodes dump\n");
	unsigned i = 0;
	while(i < LAST_NODE_INDEX)
	{
		print_node(i);
		if(nodes[i].next == LAST_NODE_INDEX) break;
		i = nodes[i].next;
	}
}

/*
void x86_ap_mm_setup()
{
	int i;
	for(i = 0; i < 1024; ++i)
		AP_KPT[i] = (0x1000 * i) | 3;

	AP_PD[0] = (uint32_t)AP_KPT | 3;
	//PD[1] = 0x400083;
	//AP_LKPT[1023] = (uint32_t)AP_PD | 3;

	asm volatile("mov %%eax, %%cr3;"::"a"(AP_PD));
	asm volatile("mov %%cr4, %%eax; or %0, %%eax; mov %%eax, %%cr4;"::"g"(0x10));
	asm volatile("mov %%cr0, %%eax; or %0, %%eax; mov %%eax, %%cr0;"::"g"(0x80000000));
}
*/
