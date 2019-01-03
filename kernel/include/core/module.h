#ifndef _MODULE_H
#define _MODULE_H

#define __CAT(a, b) a ## b
#define MODULE_INIT(name, i, f) \
    __section(".__minit") void * __CAT(__minit_, name) = i;\
    __section(".__mfini") void * __CAT(__mfini_, name) = f;\

int modules_init(void);

#endif
