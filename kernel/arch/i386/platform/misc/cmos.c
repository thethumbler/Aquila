/** \file
 * x86: the PC's real time clock, in the CMOS: the date and time, which
 * the kernel reads to set its clock at boot (`arch_time_get()`). The year
 * is taken to be in the 2000s (the century register isn't read).
 */

#include <core/system.h>
#include <core/types.h>
#include <cpu/io.h>

#define RTC_SEC    0x00      /* seconds */
#define RTC_MIN    0x02      /* minutes */
#define RTC_HRS    0x04      /* hours */
#define RTC_WD     0x06      /* the day of the week */
#define RTC_DOM    0x07      /* the day of the month */
#define RTC_MON    0x08      /* the month */
#define RTC_YR     0x09      /* the year */
#define RTC_SA     0x0A      /* status register A */
#define RTC_SB     0x0B      /* status register B */

#define RTC_UIP    0x80      /* an update in progress (status A) */
#define RTC_24H    0x02      /* 24-hour mode (status B) */
#define RTC_BIN    0x04      /* binary mode, else BCD (status B) */
#define RTC_PM     0x80      /* the PM flag in the hours (12-hour mode) */

static struct ioaddr cmos;

/** Reads a CMOS register. Bit 7 of the index port disables NMIs while it
 * is set, so it is left clear: setting it would mask them from then on. */
static uint8_t cmos_reg_read(uint8_t reg)
{
    io_out8(&cmos, 0, reg & 0x7F);
    return io_in8(&cmos, 1);
}

/** The real time clock's date and time, as it keeps them. */
struct cmos_rtc {
    uint8_t yr;
    uint8_t mon;
    uint8_t mday;
    uint8_t hrs;
    uint8_t min;
    uint8_t sec;
    uint8_t wday;
};

/** Converts a BCD byte to binary. */
static inline uint8_t bcd_to_bin(uint8_t bcd)
{
    return ((bcd >> 4) & 0xF) * 10 + (bcd & 0xF);
}

/** Reads the real time clock's date and time, in binary and in 24-hour
 * time. Returns 0. */
static int cmos_time(struct cmos_rtc *rtc)
{
    uint8_t  fmt = cmos_reg_read(RTC_SB);

    /* wait for an update in progress to finish, so that the values read
     * are consistent; bounded, since a machine without a CMOS clock reads
     * all ones, an update that never ends (an update takes 2 ms at most) */
    for (int i = 0; i < 1000000 && (cmos_reg_read(RTC_SA) & RTC_UIP); ++i)
        ;

    rtc->yr   = cmos_reg_read(RTC_YR);
    rtc->mon  = cmos_reg_read(RTC_MON);
    rtc->mday = cmos_reg_read(RTC_DOM);
    rtc->hrs  = cmos_reg_read(RTC_HRS);
    rtc->min  = cmos_reg_read(RTC_MIN);
    rtc->sec  = cmos_reg_read(RTC_SEC);
    rtc->wday = cmos_reg_read(RTC_WD);

    /* the PM flag only means something in 12-hour mode */
    int pm = !(fmt & RTC_24H) && (rtc->hrs & RTC_PM);
    rtc->hrs &= ~RTC_PM;

    if (!(fmt & RTC_BIN)) {
        /* convert the values from BCD to binary */
        rtc->yr   = bcd_to_bin(rtc->yr);
        rtc->mon  = bcd_to_bin(rtc->mon);
        rtc->mday = bcd_to_bin(rtc->mday);
        rtc->hrs  = bcd_to_bin(rtc->hrs);
        rtc->min  = bcd_to_bin(rtc->min);
        rtc->sec  = bcd_to_bin(rtc->sec);
        rtc->wday = bcd_to_bin(rtc->wday);
    }

    if (!(fmt & RTC_24H)) {
        /* 12-hour mode: 12 AM is 0, 12 PM is 12 */
        rtc->hrs %= 12;
        if (pm) rtc->hrs += 12;
    }

    return 0;
}

/*
 * Reads the date and time from the real time clock, as UNIX time.
 */
int arch_time_get(struct timespec *ts)
{
    struct cmos_rtc rtc;
    cmos_time(&rtc);

    time_t time = 0;
    uint32_t yr = rtc.yr + 2000;
    uint32_t mon = rtc.mon;

    /* the formula below counts months from March, so January and
     * February are months 13 and 14 of the previous year */
    if (mon <= 2) {
        mon += 12;
        yr  -= 1;
    }

    /* the years, in days */
    time = (365 * yr) + (yr / 4) - (yr / 100) + (yr / 400);
    /* the months and days, in days */
    time += (30 * mon) + (3 * (mon + 1) / 5) + rtc.mday;
    /* UNIX time starts on January 1st, 1970 */
    time -= 719561;
    /* the days, in seconds */
    time *= 86400;
    /* and the time of day */
    time += (3600 * rtc.hrs) + (60 * rtc.min) + rtc.sec;

    ts->tv_sec = time;
    ts->tv_nsec = 0;

    return 0;
}

/**
 * Sets where the CMOS's ports are.
 *
 * @param ioaddr  its index and data ports (0x70)
 * @return 0
 */
int x86_cmos_setup(struct ioaddr *ioaddr)
{
    cmos = *ioaddr;
    printk("cmos: initializing [%p (%s)]\n", cmos.addr, ioaddr_type_str(&cmos));

    return 0;
}
