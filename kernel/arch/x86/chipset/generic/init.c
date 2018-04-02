#include <core/system.h>
#include <console/earlycon.h>
#include <chipset/misc.h>
#include <cpu/io.h>

#define PIC_MASTER	0x20
#define PIC_SLAVE	0xA0

int x86_generic_pic_init()
{
    struct __ioaddr pic_master;
    struct __ioaddr pic_slave;

    pic_master.addr = PIC_MASTER;
    pic_master.type = __IOADDR_PORT;
    pic_slave.addr  = PIC_SLAVE;
    pic_slave.type  = __IOADDR_PORT;

    return __x86_pic_setup(&pic_master, &pic_slave);
}

#define PIT_IRQ 0
void chipset_timer_setup(size_t period_ns, void (*handler)())
{
    //pit_setup(timer_freq);
    __x86_irq_handler_install(PIT_IRQ, handler);
    //hpet_timer_setup(1, x86_sched_handler);
}

int chipset_init()
{
    earlycon_init();
    printk("x86: Welcome to AquilaOS!\n");

    for (;;);

    x86_generic_pic_init();
    return 0;
}
