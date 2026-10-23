/** \file
 * The date: the PL031 real time clock, seconds since the epoch
 */

#include <core/system.h>
#include <core/types.h>
#include <cpu/io.h>
#include <platform/virt.h>

#define RTC_DR  0x000   /* data: seconds */

int arch_time_get(struct timespec *ts)
{
    ts->tv_sec  = mmio_read32(VIRT_REG(VIRT_RTC) + RTC_DR);
    ts->tv_nsec = 0;
    return 0;
}
