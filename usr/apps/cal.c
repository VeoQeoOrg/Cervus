#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/cervus.h>
#include <cervus_util.h>

#define A_INVERT  "\x1b[7m"
#define A_BOLD    "\x1b[1m"
#define A_RESET   "\x1b[0m"

static const char *MNAME[12] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
};
static int MDAYS[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

static int is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m)
{
    if (m == 2 && is_leap(y)) return 29;
    return MDAYS[m - 1];
}

static int first_dow(int y, int m)
{
    int64_t days = 0;
    for (int yr = 1970; yr < y; yr++) days += is_leap(yr) ? 366 : 365;
    int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    md[1] = is_leap(y) ? 29 : 28;
    for (int mo = 0; mo < m - 1; mo++) days += md[mo];
    return (int)((days + 4) % 7);
}

static void get_today(int *out_y, int *out_m, int *out_d)
{
    *out_y = 0; *out_m = 0; *out_d = 0;

    cervus_timespec_t ts;
    if (cervus_clock_gettime(CLOCK_REALTIME, &ts) != 0 || ts.tv_sec <= 0)
        return;

    int64_t t    = ts.tv_sec;
    int64_t days = t / 86400;

    int y = 1970;
    while (1) {
        int dy = is_leap(y) ? 366 : 365;
        if (days < dy) break;
        days -= dy;
        y++;
    }

    int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    md[1] = is_leap(y) ? 29 : 28;

    int mo = 0;
    while (mo < 12) { if (days < md[mo]) break; days -= md[mo]; mo++; }

    *out_y = y;
    *out_m = mo + 1;
    *out_d = (int)(days + 1);
}

static void print_header(int y, int m, int today_y, int today_m)
{
    char title[64];
    snprintf(title, sizeof(title), "%s %d", MNAME[m - 1], y);
    int tlen = (int)strlen(title);
    int pad  = (20 - tlen) / 2;

    for (int i = 0; i < pad; i++) putchar(' ');

    if (y == today_y && m == today_m) {
        fputs(A_INVERT A_BOLD, stdout);
        fputs(title, stdout);
        fputs(A_RESET, stdout);
    } else {
        fputs(A_BOLD, stdout);
        fputs(title, stdout);
        fputs(A_RESET, stdout);
    }
    putchar('\n');
    fputs(C_GRAY " Su Mo Tu We Th Fr Sa" A_RESET "\n", stdout);
}

static void print_month(int y, int m, int today_y, int today_m, int today_d)
{
    print_header(y, m, today_y, today_m);

    int mdays = days_in_month(y, m);
    int dow   = first_dow(y, m);

    for (int i = 0; i < dow; i++) fputs("   ", stdout);

    for (int d = 1; d <= mdays; d++) {
        int is_today = (y == today_y && m == today_m && d == today_d);

        if (is_today) {
            fprintf(stdout, " " A_INVERT A_BOLD "%2d" A_RESET, d);
        } else {
            fprintf(stdout, " %2d", d);
        }

        if (++dow == 7) {
            putchar('\n');
            dow = 0;
        }
    }
    if (dow != 0) putchar('\n');
}

static void print_help(void)
{
    fputs(
        "Usage: cal [OPTION] [[MONTH] YEAR]\n"
        "Display a calendar.\n"
        "\n"
        "  (no args)       current month\n"
        "  YEAR            all 12 months of YEAR\n"
        "  MONTH YEAR      specific month (MONTH = 1-12)\n"
        "  --help          display this help and exit\n"
        "\n"
        "Today's day is highlighted with " A_INVERT "inverted colours" A_RESET ".\n"
        "The current month header is also " A_INVERT "inverted" A_RESET ".\n",
        stdout
    );
}

int main(int argc, char **argv)
{
    int today_y, today_m, today_d;
    get_today(&today_y, &today_m, &today_d);

    int year  = today_y  ? today_y  : 2025;
    int month = today_m  ? today_m  : 1;

    const char *args[2] = {NULL, NULL};
    int real_argc = 0;
    int flag_help = 0;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (strcmp(argv[i], "--help") == 0) { flag_help = 1; continue; }
        if (real_argc < 2) args[real_argc] = argv[i];
        real_argc++;
    }

    if (flag_help) { print_help(); return 0; }

    if (real_argc == 2) {
        month = atoi(args[0]);
        year  = atoi(args[1]);
    } else if (real_argc == 1) {
        year = atoi(args[0]);
        putchar('\n');
        fprintf(stdout, "          " A_BOLD "%d" A_RESET "\n\n", year);
        for (int m = 1; m <= 12; m++) {
            print_month(year, m, today_y, today_m, today_d);
            putchar('\n');
        }
        return 0;
    }

    if (month < 1 || month > 12) {
        fputs("cal: invalid month (must be 1-12)\n", stderr);
        return 1;
    }

    putchar('\n');
    print_month(year, month, today_y, today_m, today_d);
    putchar('\n');
    return 0;
}