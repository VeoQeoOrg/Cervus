#include <stdio.h>
#include <stdint.h>
#include <sys/cervus.h>

static const int MDAYS[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
static const char *MNAME[12] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
};
static const char *WDAY[7] = {"Thu","Fri","Sat","Sun","Mon","Tue","Wed"};

static int is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
static int bcd2bin(int v) { return (v & 0x0F) + ((v >> 4) * 10); }

static int cmos_read(int reg)
{
    if (cervus_ioport_write(0x70, 1, (uint32_t)(reg & 0x7F)) < 0) return -1;
    return (int)(cervus_ioport_read(0x71, 1) & 0xFF);
}
static void cmos_wait_ready(void)
{
    for (int i = 0; i < 2000; i++) {
        cervus_ioport_write(0x70, 1, 0x0A);
        uint32_t sta = cervus_ioport_read(0x71, 1);
        if (!(sta & 0x80)) return;
    }
}

static int64_t rtc_read_unix(void)
{
    cmos_wait_ready();
    int sec  = cmos_read(0x00);
    int min  = cmos_read(0x02);
    int hour = cmos_read(0x04);
    int mday = cmos_read(0x07);
    int mon  = cmos_read(0x08);
    int year = cmos_read(0x09);
    if (sec < 0 || min < 0 || hour < 0 || mday < 0 || mon < 0 || year < 0) return 0;

    cervus_ioport_write(0x70, 1, 0x0B);
    int regb        = (int)cervus_ioport_read(0x71, 1);
    int binary_mode = (regb >= 0) && (regb & 0x04);
    int hour24      = (regb >= 0) && (regb & 0x02);

    if (!binary_mode) {
        sec  = bcd2bin(sec);
        min  = bcd2bin(min);
        mday = bcd2bin(mday);
        mon  = bcd2bin(mon);
        year = bcd2bin(year);
        if (!hour24 && (hour & 0x80)) hour = bcd2bin(hour & 0x7F) + 12;
        else                           hour = bcd2bin(hour);
    }
    year += (year < 70) ? 2000 : 1900;
    if (sec < 0 || sec > 59 || min < 0 || min > 59)   return 0;
    if (hour < 0 || hour > 23)                         return 0;
    if (mday < 1 || mday > 31)                         return 0;
    if (mon  < 1 || mon  > 12)                         return 0;
    if (year < 2000)                                   return 0;

    int64_t days = 0;
    for (int y = 1970; y < year; y++) days += is_leap(y) ? 366 : 365;
    for (int m = 1; m < mon; m++) days += MDAYS[m-1] + (m == 2 && is_leap(year) ? 1 : 0);
    days += mday - 1;
    return days * 86400LL + (int64_t)hour * 3600LL + (int64_t)min * 60LL + (int64_t)sec;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    int64_t t = rtc_read_unix();
    if (t <= 0) {
        uint64_t up = cervus_uptime_ns() / 1000000000ULL;
        fputs("  RTC not available.\n", stdout);
        printf("  Uptime: %lus\n", (unsigned long)up);
        return 0;
    }
    int     wday = (int)((t / 86400 + 4) % 7);
    int64_t days = t / 86400;
    int64_t rem  = t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    int hour = (int)(rem / 3600);
    int min  = (int)((rem % 3600) / 60);
    int sec  = (int)(rem % 60);

    int year = 1970;
    while (1) { int dy = is_leap(year) ? 366 : 365; if (days < dy) break; days -= dy; year++; }
    int mon = 0;
    while (mon < 12) {
        int dm = MDAYS[mon] + (mon == 1 && is_leap(year) ? 1 : 0);
        if (days < dm) break;
        days -= dm; mon++;
    }
    int mday = (int)days + 1;
    printf("  %s %s %2d %02d:%02d:%02d UTC %04d\n",
           WDAY[wday], MNAME[mon], mday, hour, min, sec, year);
    return 0;
}
