#ifndef _RING_H
#define _RING_H

#include <core/system.h>
#include <mm/mm.h>

#define INDEX(ring, i) ((i) % ((ring)->size))

typedef struct ring {
	char *buf;
	size_t size;
	size_t head;
	size_t tail;
} ring_t;

static inline ring_t *new_ring(size_t size)
{
	ring_t *ring = kmalloc(sizeof(ring_t));
	*ring = (ring_t){kmalloc(size), size, 0, 0};
	return ring;
}

static inline size_t ring_read(ring_t *ring, size_t n, char *buf)
{
	size_t size = n;

	while (n) {
		if (ring->head == ring->tail)	/* Ring is empty */
			break;
		if (ring->head == ring->size)
			ring->head = 0;
		*buf++ = ring->buf[ring->head++];
		n--;
	}

	return size - n;
}

static inline size_t ring_write(ring_t *ring, size_t n, char *buf)
{
	size_t size = n;

	while(n)
	{
		if(INDEX(ring, ring->head) == INDEX(ring, ring->tail) + 1)	/* Ring is full */
			break;

		if(ring->tail == ring->size)
			ring->tail = 0;
		
		ring->buf[ring->tail++] = *buf++;
		n--;
	}

	return size - n;
}

static inline size_t ring_available(ring_t *ring)
{
	if(ring->tail >= ring->head)
		return ring->tail - ring->head;

	return ring->tail + ring->size - ring->head;
}

#endif /* !_RING_H */
