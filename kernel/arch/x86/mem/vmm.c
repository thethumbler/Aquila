/**********************************************************************
 *					Virtual Memory Manager
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
#include <mm/mm.h>

typedef struct {
	uint32_t addr : 28;	/* Offseting (1GiB), 4-bytes aligned objects */
	uint32_t free : 1;	/* Free or not flag */
	uint32_t size : 26;	/* Size of one object can be up to 256MiB */
	uint32_t next : 25; /* Index of the next node */
} __packed vmm_node_t;


#define VMM_BASE		(0xD0000000)
#define VMM_NODES_SIZE	(0x00100000)
#define VMM_NODES		(VMM_BASE - VMM_NODES_SIZE)
#define NODE_ADDR(node)	(VMM_BASE + ((node).addr) * 4)
#define NODE_SIZE(node)	(((node).size) * 4)
#define LAST_NODE_INDEX	(100000)
#define MAX_NODE_SIZE	((1 << 26) - 1)

vmm_node_t *nodes = (vmm_node_t *) VMM_NODES;
void vmm_setup()
{
	/* We start by mapping the space used for nodes into physical memory */
	pmman.map(VMM_NODES, VMM_NODES_SIZE, KRW);

	/* Now we have to clear it */
	memset((void *) VMM_NODES, 0, VMM_NODES_SIZE);

	/* Setting up initial node */
	nodes[0] = (vmm_node_t){0, 1, -1, LAST_NODE_INDEX};
}

uint32_t first_free_node = 0;
uint32_t get_node()
{
	unsigned i;//, flag = 0;
	for(i = first_free_node; i < LAST_NODE_INDEX; ++i)
	{
		/*if(!flag && nodes[i].free)
		{
			first_free_node = flag = i;
		}*/
		
		if(!nodes[i].size)
			return i;
	}

	panic("Can't find an unused node");
	return -1;
}

void release_node(uint32_t i)
{
	nodes[i] = (vmm_node_t){0};
	//if(i < first_free_node)
	//	first_free_node = i;
}

uint32_t get_first_fit_free_node(uint32_t size)
{
	unsigned i = first_free_node;
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
	printk("   |_ Addr   : %x\n", NODE_ADDR(nodes[i]));
	printk("   |_ free ? : %s\n", nodes[i].free?"yes":"no");
	printk("   |_ Size   : %d B [ %d KiB ]\n",
		NODE_SIZE(nodes[i]), NODE_SIZE(nodes[i])/1024 );
	printk("   |_ Next   : %d\n", nodes[i].next );
}

void *kmalloc(size_t size)
{
	size = (size + 3)/4;	/* size in 4-bytes units */

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

	pmman.map(NODE_ADDR(nodes[i]), NODE_SIZE(nodes[i]), KRW);
	return (void*)NODE_ADDR(nodes[i]);
}

void kfree(void *_ptr)
{
	uintptr_t ptr = (uintptr_t) _ptr;

	if(ptr < VMM_BASE)	/* That's not even allocatable */
		return;

	/* Look for the node containing _ptr */
	/* NOTE: We don't use a function for looking for the node, since we also
	   need to know the first free node of a sequence of free nodes before our
	   target node */

	unsigned i = 0, j = -1;

	while(!(ptr >= NODE_ADDR(nodes[i])
		&& ptr < (uintptr_t)(NODE_ADDR(nodes[i]) + NODE_SIZE(nodes[i]))))
	{
		if(nodes[i].free)
		{
			if(j == -1U) 
				j = i; /* Set first free node index */
		} else j = -1U;	/* Invalidate first free node index */

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
			n = nodes[i].next;
		} else
			break;
	}

	if(j != i && j != -1U)	/* I must be lucky, aren't I always ;) */
		kfree((void*)NODE_ADDR(nodes[j]));
	// else
	// 	unmap_from_physical(NODE_ADDR(nodes[i]), NODE_SIZE(nodes[i]));
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
