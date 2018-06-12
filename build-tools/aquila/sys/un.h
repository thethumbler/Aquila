#ifndef _SYS_UN_H
#define _SYS_UN_H

#include <bits/socket.h>

#define SUN_PATH_MAX    128

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[SUN_PATH_MAX];
};

#endif /* ! _SYS_UN_H */
