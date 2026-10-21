#ifndef _NET_IF_H
#define _NET_IF_H

/* network interfaces (the kernel's requests, <aquila/if.h>), and their
 * names and indexes */

#include <sys/socket.h>
#include <aquila/if.h>

#ifdef __cplusplus
extern "C" {
#endif

struct if_nameindex {
    unsigned int if_index;
    char *if_name;
};

unsigned int if_nametoindex(const char *name);
char *if_indextoname(unsigned int index, char *name);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *ptr);

#ifdef __cplusplus
}
#endif

#endif /* ! _NET_IF_H */
