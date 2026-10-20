#ifndef _AQUILA_UTSNAME_H
#define _AQUILA_UTSNAME_H

#define MAX_LENGTH 64

struct utsname {
    char  sysname[MAX_LENGTH];
    char  nodename[MAX_LENGTH];
    char  release[MAX_LENGTH];
    char  version[MAX_LENGTH];
    char  machine[MAX_LENGTH];
};

#endif /* ! _AQUILA_UTSNAME_H */
