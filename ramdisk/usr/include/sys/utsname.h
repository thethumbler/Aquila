#ifndef _UTSNAME
#define _UTSNAME

#define MAX_LENGTH 64

struct utsname {
    char  sysname[MAX_LENGTH];
    char  nodename[MAX_LENGTH];
    char  release[MAX_LENGTH];
    char  version[MAX_LENGTH];
    char  machine[MAX_LENGTH];
};

int uname(struct utsname *name);

#endif /* ! _UTSNAME */
