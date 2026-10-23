/** \file
 * Packet buffers (net/netbuf.h): NETBUF_SIZE bytes each, two to a page
 * of memory, kept on a free list once used
 */

#include <core/system.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <net/netbuf.h>

/* the free netbufs, and how many; past NETBUF_KEEP free, a page whose
 * netbufs are all free goes back to the page allocator (fewer, and the
 * next packets would take a page again at once) */
#define NETBUF_KEEP     64
#define NETBUF_PER_PAGE (PAGE_SIZE / NETBUF_SIZE)

static struct netbuf *free_netbufs;
static size_t free_count;

static void free_push(struct netbuf *nb)
{
    nb->free_prev = NULL;
    nb->next = free_netbufs;

    if (free_netbufs)
        free_netbufs->free_prev = nb;

    free_netbufs = nb;
    free_count++;
}

static void free_remove(struct netbuf *nb)
{
    if (nb->free_prev)
        nb->free_prev->next = nb->next;
    else
        free_netbufs = nb->next;

    if (nb->next)
        nb->next->free_prev = nb->free_prev;

    free_count--;
}

struct netbuf *netbuf_alloc(void)
{
    if (!free_netbufs) {
        struct vm_page *vm_page = mm_page_alloc();

        if (!vm_page)
            return NULL;

        uint8_t *page = arch_phys_map(vm_page->paddr, PAGE_SIZE);

        for (size_t off = 0; off < PAGE_SIZE; off += NETBUF_SIZE) {
            struct netbuf *nb = (struct netbuf *) (page + off);
            nb->page = vm_page->paddr;
            free_push(nb);
        }
    }

    struct netbuf *nb = free_netbufs;
    free_remove(nb);

    /* its page's netbufs in use: the page's reference count */
    mm_page_incref(nb->page);

    nb->next  = NULL;
    nb->netif = NULL;
    nb->data  = (uint8_t *) (nb + 1) + NETBUF_HEADROOM;
    nb->len   = 0;

    return nb;
}

void netbuf_free(struct netbuf *nb)
{
    free_push(nb);
    mm_page_decref(nb->page);

    if (mm_page_ref(nb->page) || free_count <= NETBUF_KEEP)
        return;

    /* the page's netbufs are all free: off the list, the page back */
    uint8_t *page = (uint8_t *) ((uintptr_t) nb & ~(uintptr_t) PAGE_MASK);

    for (size_t i = 0; i < NETBUF_PER_PAGE; ++i)
        free_remove((struct netbuf *) (page + i * NETBUF_SIZE));

    mm_page_dealloc(nb->page);
}
