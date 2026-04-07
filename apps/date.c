#include "../apps/cervus_user.h"

static const int MDAYS[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
static const char *MNAME[12] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
};
static const char *WDAY[7] = {"Thu","Fri","Sat","Sun","Mon","Tue","Wed"};

static int is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

CERVUS_MAIN(date_main) {
    (void)argc; (void)argv;
    cervus_timespec_t ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0 || ts.tv_sec == 0) {
        uint64_t up = uptime_ns() / 1000000000ULL;
        ws("  System clock not available.\n");
        printf("  Uptime: %llus\n", (unsigned long long)up);
        exit(0);
    }

    int64_t t    = ts.tv_sec;
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
        if (days < dm) break; days -= dm; mon++;
    }
    int mday = (int)days + 1;

    printf("  %s %s %2d %02d:%02d:%02d UTC %04d\n",
           WDAY[wday], MNAME[mon], mday, hour, min, sec, year);
    exit(0);
}