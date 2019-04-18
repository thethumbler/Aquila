#include <core/system.h>
#include <core/types.h>
#include <cpu/io.h>

#define RTC_SEC    0x00      /* Seconds */
#define RTC_MIN    0x02      /* Minutes */
#define RTC_HRS    0x04      /* Hours */
#define RTC_WD     0x06      /* Weekday */
#define RTC_DOM    0x07      /* Day of Month */
#define RTC_MON    0x08      /* Month */
#define RTC_YR     0x09      /* Year */
#define RTC_SA     0x0A      /* Status Register A */
#define RTC_SB     0x0B      /* Status Register B */

#define RTC_BIN    0x04      /* Binary mode */

static struct ioaddr cmos;

static uint8_t cmos_reg_read(uint8_t reg)
{
    io_out8(&cmos, 0, (1 << 7) | (reg));
    return io_in8(&cmos, 1);
}

struct cmos_rtc {
    uint8_t yr;
    uint8_t mon;
    uint8_t mday;
    uint8_t hrs;
    uint8_t min;
    uint8_t sec;
    uint8_t wday;
};

static inline uint8_t bcd_to_bin(uint8_t bcd)
{
    return ((bcd >> 4) & 0xF) * 10 + (bcd & 0xF);
}

static int cmos_time(struct cmos_rtc *rtc)
{
    uint8_t  fmt = cmos_reg_read(RTC_SB);

    rtc->yr   = cmos_reg_read(RTC_YR);
    rtc->mon  = cmos_reg_read(RTC_MON);
    rtc->mday = cmos_reg_read(RTC_DOM);
    rtc->hrs  = cmos_reg_read(RTC_HRS);
    rtc->min  = cmos_reg_read(RTC_MIN);
    rtc->sec  = cmos_reg_read(RTC_SEC);
    rtc->wday = cmos_reg_read(RTC_WD);

    if (!(fmt & RTC_BIN)) {
        /* convert all values to binary */
        rtc->yr   = bcd_to_bin(rtc->yr);
        rtc->mon  = bcd_to_bin(rtc->mon);
        rtc->mday = bcd_to_bin(rtc->mday);
        rtc->hrs  = bcd_to_bin(rtc->hrs);
        rtc->min  = bcd_to_bin(rtc->min);
        rtc->sec  = bcd_to_bin(rtc->sec);
        rtc->wday = bcd_to_bin(rtc->wday);
    }

    return 0;
}

/* FIXME: Should be elsewhere */
int arch_time_get(struct timespec *ts)
{
    struct cmos_rtc rtc;
    cmos_time(&rtc);

    time_t time = 0;
    uint32_t yr = rtc.yr + 2000;

    /* Convert years to days */
    time = (365 * yr) + (yr / 4) - (yr / 100) + (yr / 400);
    /* Convert months to days */
    time += (30 * rtc.mon) + (3 * (rtc.mon + 1) / 5) + rtc.mday;
    /* UNIX time starts on January 1st, 1970 */
    time -= 719561;
    /* Convert days to seconds */
    time *= 86400;
    /* Add hours, minutes and seconds */
    time += (3600 * rtc.hrs) + (60 * rtc.min) + rtc.sec;

    ts->tv_sec = time;
    ts->tv_nsec = 0;

    return 0;
}

int x86_cmos_setup(struct ioaddr *ioaddr)
{
    cmos = *ioaddr;
    printk("CMOS: Initializing [%p (%s)]\n", cmos.addr, ioaddr_type_str(&cmos));

    return 0;
}
