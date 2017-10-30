#include <core/system.h>
#include <dev/dev.h>

dev_t *devices[] =
{
	&i8042dev,
	&ps2kbddev,
	&condev,
    &pcidev,
    &atadev,
	NULL
};

void devman_init()
{
    printk("[0] Kernel: devman -> init()\n");

	foreach (device, devices) {
		device->probe();
	}
}
