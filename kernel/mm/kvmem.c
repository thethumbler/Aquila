#include <core/system.h>
#include <core/panic.h>
#include <core/assert.h>

#include <mm/pmap.h>
#include <mm/mm.h>
#include <mm/buddy.h> /* XXX */
#include <mm/vm.h>

uintptr_t __stack_chk_guard = 0xDEADBEEF;
void __stack_chk_fail(void)
{
    panic("stack smashing detected");
}

struct queue *malloc_types = QUEUE_NEW();

MALLOC_DEFINE(M_BUFFER, "buffer", "generic buffer");
MALLOC_DEFINE(M_RINGBUF, "ring-buffer", "ringbuffer structure");
MALLOC_DEFINE(M_QUEUE, "queue", "queue structure");
MALLOC_DEFINE(M_QNODE, "queue-node", "queue node structure");
MALLOC_DEFINE(M_HASHMAP, "hashmap", "hashmap structure");
MALLOC_DEFINE(M_HASHMAP_NODE, "hashmap-node", "hashmap node structure");

int debug_kmalloc = 0;

struct kvmem_node {
    uint32_t addr : 28; /* Offseting (1GiB), 4-bytes aligned objects */
    uint32_t free : 1;  /* Free or not flag */
    uint32_t size : 26; /* Size of one object can be up to 256MiB */
    uint32_t next : 25; /* Index of the next node */
    struct malloc_type *type;
};

size_t kvmem_used;
size_t kvmem_obj_cnt;

#define KVMEM_BASE       ARCH_KVMEM_BASE

#define NODE_ADDR(node) (KVMEM_BASE + ((node).addr) * 4U)
#define NODE_SIZE(node) (((node).size) * 4U)
#define LAST_NODE_INDEX (100000)
#define MAX_NODE_SIZE   ((1UL << 26) - 1)

#if 0
struct vm_entry kvmem_nodes = {
    .base  = KVMEM_NODES,
    .size  = KVMEM_NODES_SIZE,
    .flags = VM_KRW,
};
#endif

//struct kvmem_node *nodes = (struct kvmem_node *) KVMEM_NODES;
struct kvmem_node nodes[LAST_NODE_INDEX];

void kvmem_setup(void)
{
    /* Setting up initial node */
    nodes[0].addr = 0;
    nodes[0].free = 1;
    nodes[0].size = -1;
    nodes[0].next = LAST_NODE_INDEX;

    /* We have to set qnode to an arbitrary value since
     * enqueue will use kmalloc which would try to enqueue
     * M_QNODE type if qnode == NULL, gettings us in an
     * infinite loop
     */
    M_QNODE.qnode = (void *) 0xDEADBEEF;
    M_QNODE.qnode = enqueue(malloc_types, &M_QNODE);
}

uint32_t first_free_node = 0;
static uint32_t get_node(void)
{
    for (unsigned i = first_free_node; i < LAST_NODE_INDEX; ++i) {
        if (!nodes[i].size) {
            return i;
        }
    }

    panic("Can't find an unused node");
}

void release_node(uint32_t i)
{
    if (nodes[i].type)
        nodes[i].type->nr--;

    memset(&nodes[i], 0, sizeof(struct kvmem_node));
    nodes[i].free = 1;
}

uint32_t get_first_fit_free_node(uint32_t size)
{
    unsigned i = first_free_node;

    while (!(nodes[i].free && nodes[i].size >= size)) {
        if (nodes[i].next == LAST_NODE_INDEX)
            panic("Can't find a free node");
        i = nodes[i].next;
    }

    return i;
}

void print_node(unsigned i)
{
    printk("Node[%d]\n", i);
    printk("   |_ Addr   : %x\n", NODE_ADDR(nodes[i]));
    printk("   |_ free?  : %s\n", nodes[i].free?"yes":"no");
    printk("   |_ Size   : %d B [ %d KiB ]\n",
        NODE_SIZE(nodes[i]), NODE_SIZE(nodes[i])/1024);
    printk("   |_ Next   : %d\n", nodes[i].next );
}

void *kmalloc(size_t size, struct malloc_type *type, int flags)
{
    /* round size to 4-byte units */
    size = (size + 3)/4;

    /* Look for a first fit free node */
    unsigned i = get_first_fit_free_node(size);

    /* Mark it as used */
    nodes[i].free = 0;

    /* Split the node if necessary */
    if (nodes[i].size > size) {
        unsigned n = get_node();

        nodes[n].addr = nodes[i].addr + size;
        nodes[n].free = 1;
        nodes[n].size = nodes[i].size - size;
        nodes[n].next = nodes[i].next;

        nodes[i].next = n;
        nodes[i].size = size;
    }

    nodes[i].type = type;
    type->nr++;

    type->total += NODE_SIZE(nodes[i]);
    kvmem_used += NODE_SIZE(nodes[i]);
    kvmem_obj_cnt++;

    vaddr_t map_base = PAGE_ALIGN(NODE_ADDR(nodes[i]));
    vaddr_t map_end  = PAGE_ROUND(NODE_ADDR(nodes[i]) + NODE_SIZE(nodes[i]));
    size_t  map_size = (map_end - map_base)/PAGE_SIZE;

    if (map_size) {
        struct vm_entry vm_entry;

        vm_entry.paddr = 0,
        vm_entry.base  = map_base,
        vm_entry.size  = map_size * PAGE_SIZE,
        vm_entry.flags = VM_KRW,

        vm_map(&kvm_space, &vm_entry);
    }

    if (type->qnode == NULL) {
        type->qnode = enqueue(malloc_types, type);
    }

    void *obj = (void *) NODE_ADDR(nodes[i]);

    if (flags & M_ZERO) {
        memset(obj, 0, size * 4);
    }

    return obj;
}

void kfree(void *_ptr)
{
    //printk("kfree(%p)\n", _ptr);
    uintptr_t ptr = (uintptr_t) _ptr;

    if (ptr < KVMEM_BASE)  /* That's not even allocatable */
        return;

    /* Look for the node containing _ptr -- merge sequential free nodes */
    size_t cur_node = 0, prev_node = 0;

    while (ptr != NODE_ADDR(nodes[cur_node])) {
        /* check if current and previous node are free */
        if (cur_node && nodes[cur_node].free && nodes[prev_node].free) {
            /* check for overflow */
            if ((uintptr_t) (nodes[cur_node].size + nodes[prev_node].size) <= MAX_NODE_SIZE) {
                nodes[prev_node].size += nodes[cur_node].size;
                nodes[prev_node].next  = nodes[cur_node].next;
                release_node(cur_node);
                cur_node = nodes[prev_node].next;
                continue;
            }
        }

        prev_node = cur_node;
        cur_node = nodes[cur_node].next;

        if (cur_node == LAST_NODE_INDEX)  /* Trying to free unallocated node */
            goto done;
    }

    if (nodes[cur_node].free)  { /* Node is already free, dangling pointer? */
        printk("double free detected at %p\n", ptr);
        if (nodes[cur_node].type)
            printk("object type: %s\n", nodes[cur_node].type->name);
        panic("double free");
        //goto done;
    }

    /* First we mark our node as free */
    nodes[cur_node].free = 1;
    
    if (nodes[cur_node].type) {
        nodes[cur_node].type->total -= NODE_SIZE(nodes[cur_node]);
        nodes[cur_node].type->nr--;
        nodes[cur_node].type = NULL;
    }

    if (debug_kmalloc)
        printk("NODE_SIZE %d\n", NODE_SIZE(nodes[cur_node]));

    kvmem_used -= NODE_SIZE(nodes[cur_node]);
    kvmem_obj_cnt--;

    /* Now we merge all free nodes ahead -- except the last node */
    while (nodes[cur_node].next < LAST_NODE_INDEX && nodes[cur_node].free) {
        /* check if current and previous node are free */
        if (cur_node && nodes[cur_node].free && nodes[prev_node].free) {
            /* check for overflow */
            if ((uintptr_t) (nodes[cur_node].size + nodes[prev_node].size) <= MAX_NODE_SIZE) {
                nodes[prev_node].size += nodes[cur_node].size;
                nodes[prev_node].next  = nodes[cur_node].next;
                release_node(cur_node);
                cur_node = nodes[prev_node].next;
                continue;
            }
        }

        prev_node = cur_node;
        cur_node = nodes[cur_node].next;
    }

    cur_node = 0;
    while (nodes[cur_node].next < LAST_NODE_INDEX) {
        if (nodes[cur_node].free) {
            //struct vm_entry vm_entry = {0};

            //vm_entry.paddr = 0;
            //vm_entry.base  = NODE_ADDR(nodes[cur_node]);
            //vm_entry.size  = NODE_SIZE(nodes[cur_node]);
            //vm_entry.flags = VM_KRW;

            vaddr_t vaddr = NODE_ADDR(nodes[cur_node]);
            size_t  size  = NODE_SIZE(nodes[cur_node]);

            //vm_unmap(&kvm_space, &vm_entry);

            /* XXX */
            if (size < PAGE_SIZE) goto next;

            vaddr_t sva = PAGE_ROUND(vaddr);
            vaddr_t eva = PAGE_ALIGN(vaddr + size);

            size_t nr = (eva - sva)/PAGE_SIZE;

            while (nr--) {
                paddr_t paddr = arch_page_get_mapping(kvm_space.pmap, sva);

                if (paddr) {
                    pmap_remove(kvm_space.pmap, sva, sva + PAGE_SIZE);
                    buddy_free(BUDDY_ZONE_NORMAL, paddr, PAGE_SIZE);
                }

                sva += PAGE_SIZE;
            }
        }

next:
        cur_node = nodes[cur_node].next;
    }

done:
    return;
}

void dump_nodes(void)
{
    printk("Nodes dump\n");
    unsigned i = 0;
    while (i < LAST_NODE_INDEX) {
        print_node(i);
        if (nodes[i].next == LAST_NODE_INDEX) break;
        i = nodes[i].next;
    }
}
