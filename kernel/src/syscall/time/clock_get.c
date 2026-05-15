#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/apic/apic.h"
#include "../../../include/io/ports.h"
#include <stdbool.h>

static int _rtc_bcd2bin(int v) { return (v & 0x0F) + ((v >> 4) * 10); }

static uint8_t _cmos_read(uint8_t reg)
{
    outb(0x70, reg & 0x7F);
    io_wait();
    return inb(0x71);
}

static bool _rtc_updating(void)
{
    outb(0x70, 0x0A);
    io_wait();
    return (inb(0x71) & 0x80) != 0;
}

static int64_t _rtc_read_unix(void)
{
    for (int i = 0; i < 1000 && _rtc_updating(); i++)
        io_wait();

    uint8_t sec, min, hour, mday, mon, year;
    uint8_t sec2, min2;
    do {
        sec  = _cmos_read(0x00);
        min  = _cmos_read(0x02);
        hour = _cmos_read(0x04);
        mday = _cmos_read(0x07);
        mon  = _cmos_read(0x08);
        year = _cmos_read(0x09);
        sec2 = _cmos_read(0x00);
        min2 = _cmos_read(0x02);
    } while (sec != sec2 || min != min2);

    uint8_t regb = _cmos_read(0x0B);
    int binary_mode = (regb & 0x04);
    int hour24      = (regb & 0x02);

    if (!binary_mode) {
        sec  = (uint8_t)_rtc_bcd2bin(sec);
        min  = (uint8_t)_rtc_bcd2bin(min);
        mday = (uint8_t)_rtc_bcd2bin(mday);
        mon  = (uint8_t)_rtc_bcd2bin(mon);
        year = (uint8_t)_rtc_bcd2bin(year);
        if (!hour24 && (hour & 0x80))
            hour = (uint8_t)(_rtc_bcd2bin(hour & 0x7F) + 12);
        else
            hour = (uint8_t)_rtc_bcd2bin(hour);
    }

    int iyear = year + ((year < 70) ? 2000 : 1900);

    if (sec > 59 || min > 59 || hour > 23) return 0;
    if (mday < 1 || mday > 31)             return 0;
    if (mon  < 1 || mon  > 12)             return 0;
    if (iyear < 2000)                      return 0;

    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    #define IS_LEAP(y) (((y) % 4 == 0 && (y) % 100 != 0) || (y) % 400 == 0)

    int64_t days = 0;
    for (int y = 1970; y < iyear; y++)
        days += IS_LEAP(y) ? 366 : 365;
    for (int m = 1; m < (int)mon; m++)
        days += mdays[m - 1] + (m == 2 && IS_LEAP(iyear) ? 1 : 0);
    days += mday - 1;

    return days * 86400LL + (int64_t)hour * 3600LL + (int64_t)min * 60LL + (int64_t)sec;
}

static volatile int64_t  g_rtc_base_sec    = 0;
static volatile uint64_t g_rtc_base_ns     = 0;
static volatile bool     g_rtc_initialized = false;

static void _ensure_rtc_base(void)
{
    if (g_rtc_initialized) return;
    int64_t t = _rtc_read_unix();
    if (t > 0) {
        g_rtc_base_sec    = t;
        g_rtc_base_ns     = hpet_elapsed_ns();
        g_rtc_initialized = true;
    }
}

int64_t sys_clock_get(uint64_t id, uint64_t ts_ptr)
{
    (void)id;
    if (!ts_ptr) return -EINVAL;

    cervus_timespec_t ts;

    if (id == 1) {
        uint64_t ns   = hpet_elapsed_ns();
        ts.tv_sec     = (int64_t)(ns / 1000000000ULL);
        ts.tv_nsec    = (int64_t)(ns % 1000000000ULL);
    } else {
        _ensure_rtc_base();

        if (g_rtc_initialized) {
            uint64_t now_ns  = hpet_elapsed_ns();
            uint64_t delta   = now_ns - g_rtc_base_ns;
            int64_t  real_s  = g_rtc_base_sec + (int64_t)(delta / 1000000000ULL);
            uint64_t real_ns = delta % 1000000000ULL;
            ts.tv_sec  = real_s;
            ts.tv_nsec = (int64_t)real_ns;
        } else {
            uint64_t ns  = hpet_elapsed_ns();
            ts.tv_sec    = (int64_t)(ns / 1000000000ULL);
            ts.tv_nsec   = (int64_t)(ns % 1000000000ULL);
        }
    }

    return syscall_copy_to_user((void *)ts_ptr, &ts, sizeof(ts));
}
