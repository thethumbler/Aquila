#ifndef _FS_STAT_H
#define _FS_STAT_H

#include <core/system.h>

#include <core/types.h>
#include <aquila/stat.h>

/* struct aquila_stat, S_IF* and S_IS*() are defined in <aquila/stat.h> */

#define S_ENFMT     0002000 /* enforcement-mode locking */

#define S_IREAD     S_IRUSR /* read permission, owner */
#define S_IWRITE    S_IWUSR /* write permission, owner */
#define S_IEXEC     S_IXUSR /* execute/search permission, owner */

#endif /* ! _FS_STAT_H */
