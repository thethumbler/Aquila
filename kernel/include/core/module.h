#ifndef _MODULE_H
#define _MODULE_H

/* Pastes two tokens together. */
#define __CAT(a, b) a ## b

/* Declares a built-in module: `i` is its init function, which runs at
 * boot (modules_init()), and `f` its exit function, which isn't used.
 * The linker gathers the init functions in the .__minit section. */
#define MODULE_INIT(name, i, f) \
    __section(".__minit") void * __CAT(__minit_, name) = i;\
    __section(".__mfini") void * __CAT(__mfini_, name) = f;\

/* Runs every built-in module's init function. */
int modules_init(void);

#endif
