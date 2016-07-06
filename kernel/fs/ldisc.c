#include <core/system.h>

struct ldisc {
	char *cook;	/* Cooking buffer */
	size_t pos;	/* Position in buffer */
	size_t size; /* Buffer size */

	struct ring *in;	/* Input ring */
}