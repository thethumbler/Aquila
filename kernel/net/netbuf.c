/** \file
 * Packet buffers (see `net/netbuf.h`).
 *
 * A netbuf holds one packet: `NETBUF_SIZE` bytes, two to a page of
 * memory. Freed netbufs are kept on a free list, ready for the next
 * packets, and a page goes back to the page allocator only when all its
 * netbufs are free and the list has enough without them.
 */

#include <core/system.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <net/netbuf.h>

/* The free netbufs, and how many there are. Once more than NETBUF_KEEP
 * are free, a page whose netbufs are all free goes back to the page
 * allocator (keeping fewer would only make the next packets take a page
 * again at once). */
#define NETBUF_KEEP     64
#define NETBUF_PER_PAGE (PAGE_SIZE / NETBUF_SIZE)

static struct netbuf *free_netbufs;
static size_t free_count;

/** Puts a netbuf on the free list. */
static void free_push(struct netbuf *nb)
{
    nb->free_prev = NULL;
    nb->next = free_netbufs;

    if (free_netbufs)
        free_netbufs->free_prev = nb;

    free_netbufs = nb;
    free_count++;
}

/** Takes a netbuf off the free list. */
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

/**
 * Allocates a netbuf, empty, with room for the headers that will be put
 * in front of its data.
 *
 * @return the netbuf, or NULL if there was no memory for it
 */
struct netbuf *netbuf_alloc(void)
{
    if (!free_netbufs) {
        struct vm_page *vm_page = mm_page_alloc();

        if (!vm_page)
            return NULL;

        uint8_t *page = arch_phys_map(vm_page->paddr, PAGE_SIZE);

        if (!page) {
            mm_page_dealloc(vm_page->paddr);
            return NULL;
        }

        for (size_t off = 0; off < PAGE_SIZE; off += NETBUF_SIZE) {
            struct netbuf *nb = (struct netbuf *) (page + off);
            nb->page = vm_page->paddr;
            free_push(nb);
        }
    }

    struct netbuf *nb = free_netbufs;
    free_remove(nb);

    /* the page's reference count is how many of its netbufs are in use */
    mm_page_incref(nb->page);

    nb->next  = NULL;
    nb->netif = NULL;
    nb->data  = (uint8_t *) (nb + 1) + NETBUF_HEADROOM;
    nb->len   = 0;

    return nb;
}

/**
 * Frees a netbuf.
 *
 * @param nb  the netbuf
 */
void netbuf_free(struct netbuf *nb)
{
    free_push(nb);
    mm_page_decref(nb->page);

    if (mm_page_ref(nb->page) || free_count <= NETBUF_KEEP)
        return;

    /* all the page's netbufs are free: take them off the list, and give
     * the page back */
    uint8_t *page = (uint8_t *) ((uintptr_t) nb & ~(uintptr_t) PAGE_MASK);

    for (size_t i = 0; i < NETBUF_PER_PAGE; ++i)
        free_remove((struct netbuf *) (page + i * NETBUF_SIZE));

    mm_page_dealloc(nb->page);
}
