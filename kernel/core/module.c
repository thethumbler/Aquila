#include <core/system.h>
#include <core/module.h>

extern char __minit, __minit_end;

typedef void (*__ctor)(void);

int modules_init(void)
{
    printk("kernel: loading builtin modules\n");

    /* Initalize built-in modules */
    __ctor *f = (void *) &__minit;
    __ctor *g = (void *) &__minit_end;

    size_t nr = (g - f);

    for (size_t i = 0; i < nr; ++i) {
        if (f[i]) {
            //((int (*)())f[i])();
            f[i]();
        }
    }

    return 0;
}
