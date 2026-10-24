/** \file
 * The built-in modules: the drivers and filesystems that are part of the
 * kernel. Each declares an init function with `MODULE_INIT()`, which the
 * linker gathers in the `__minit` section, and they run at boot, in link
 * order.
 */

#include <core/system.h>
#include <core/module.h>

/* A module's init function. */
typedef void (*__ctor)(void);

extern __ctor __minit[], __minit_end[];

/**
 * Runs the init function of every built-in module, at boot.
 *
 * @return 0
 */
int modules_init(void)
{
    printk("kernel: loading builtin modules\n");

    for (__ctor *f = __minit; f < __minit_end; ++f) {
        if (*f)
            (*f)();
    }

    return 0;
}
