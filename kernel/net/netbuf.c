/** \file
 * Packet buffers (net/netbuf.h): NETBUF_SIZE bytes each, two to a page
 * of memory, kept on a free list once used
 */

#include <core/system.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <net/netbuf.h>

static struct netbuf *free_netbufs;

struct netbuf *netbuf_alloc(void)
{
    if (!free_netbufs) {
        struct vm_page *vm_page = mm_page_alloc();

        if (!vm_page)
            return NULL;

        uint8_t *page = arch_phys_map(vm_page->paddr, PAGE_SIZE);

        for (size_t off = 0; off < PAGE_SIZE; off += NETBUF_SIZE) {
            struct netbuf *nb = (struct netbuf *) (page + off);
            nb->next = free_netbufs;
            free_netbufs = nb;
        }
    }

    struct netbuf *nb = free_netbufs;
    free_netbufs = nb->next;

    nb->next  = NULL;
    nb->netif = NULL;
    nb->data  = (uint8_t *) (nb + 1) + NETBUF_HEADROOM;
    nb->len   = 0;

    return nb;
}

void netbuf_free(struct netbuf *nb)
{
    nb->next = free_netbufs;
    free_netbufs = nb;
}
