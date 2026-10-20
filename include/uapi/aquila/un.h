#ifndef _AQUILA_UN_H
#define _AQUILA_UN_H

#include <aquila/socket.h>

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[128];
};

#endif /* ! _AQUILA_UN_H */
