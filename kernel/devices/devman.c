#include <core/system.h>
#include <dev/dev.h>

dev_t *devices[] =
{
	&i8042dev,
	&ps2kbddev,
	NULL
};

void devman_init()
{
	foreach(device, devices)
	{
		device->probe();
	}
}