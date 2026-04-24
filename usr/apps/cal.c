#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char *MNAME[12] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
};
static int MDAYS[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

static int is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

static int first_dow(int y, int m)
{
    int64_t days = 0;
    for (int yr = 1970; yr < y; yr++) days += is_leap(yr) ? 366 : 365;
    int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    md[1] = is_leap(y) ? 29 : 28;
    for (int mo = 0; mo < m - 1; mo++) days += md[mo];
    return (int)((days + 4) % 7);
}

static void print_month(int y, int m)
{
    const char *mn = MNAME[m - 1];
    int mnlen = (int)strlen(mn);
    int pad   = (20 - mnlen - 5) / 2;
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s %d\n", mn, y);
    fputs(" Su Mo Tu We Th Fr Sa\n", stdout);

    int mdays = MDAYS[m - 1];
    if (m == 2 && is_leap(y)) mdays = 29;
    int dow = first_dow(y, m);
    for (int i = 0; i < dow; i++) fputs("   ", stdout);
    for (int d = 1; d <= mdays; d++) {
        printf(" %2d", d);
        if (++dow == 7) { putchar('\n'); dow = 0; }
    }
    if (dow != 0) putchar('\n');
}

int main(int argc, char **argv)
{
    int year = 2025, month = 1;
    cervus_timespec_t ts;
    if (cervus_clock_gettime(CLOCK_REALTIME, &ts) == 0 && ts.tv_sec > 0) {
        int64_t t = ts.tv_sec;
        int y = 1970;
        int64_t days = t / 86400;
        while (1) { int dy = is_leap(y) ? 366 : 365; if (days < dy) break; days -= dy; y++; }
        int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        md[1] = is_leap(y) ? 29 : 28;
        int mo = 0;
        while (mo < 12) { if (days < md[mo]) break; days -= md[mo]; mo++; }
        year = y; month = mo + 1;
    }

    int real_argc = 0;
    const char *args[2] = {NULL, NULL};
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (real_argc < 2) args[real_argc] = argv[i];
        real_argc++;
    }

    if (real_argc == 2) {
        month = atoi(args[0]);
        year  = atoi(args[1]);
    } else if (real_argc == 1) {
        year = atoi(args[0]);
        printf("\n               %d\n\n", year);
        for (int m = 1; m <= 12; m++) { print_month(year, m); putchar('\n'); }
        return 0;
    }

    if (month < 1 || month > 12) {
        fputs("cal: invalid month\n", stdout);
        return 1;
    }
    putchar('\n');
    print_month(year, month);
    return 0;
}
