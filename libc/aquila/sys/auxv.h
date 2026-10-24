#ifndef _SYS_AUXV_H
#define _SYS_AUXV_H

/* The auxiliary vector's types (AT_PAGESZ, AT_ENTRY, ...), and
 * getauxval(), as on Linux. */

#include <aquila/auxv.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned long getauxval(unsigned long type);

#ifdef __cplusplus
}
#endif

#endif /* ! _SYS_AUXV_H */
