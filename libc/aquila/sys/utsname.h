#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H

/* struct utsname, shared with the kernel */
#include <aquila/utsname.h>

int uname(struct utsname *name);

#endif /* ! _SYS_UTSNAME_H */
