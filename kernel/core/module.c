#include <core/system.h>
#include <core/module.h>

typedef void (*__ctor)(void);

extern __ctor __minit[], __minit_end[];

int modules_init(void)
{
    printk("kernel: loading builtin modules\n");

    /* Initalize built-in modules */
    for (__ctor *f = __minit; f < __minit_end; ++f) {
        if (*f)
            (*f)();
    }

    return 0;
}
