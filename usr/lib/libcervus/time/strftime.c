#include <time.h>
#include <stddef.h>
#include <libcervus.h>

static const char *const g_wday[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

static const char *const g_mon[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

typedef struct {
    char  *s;
    size_t max;
    size_t n;
    int    full;
} out_t;

static void put(out_t *o, char c)
{
    if (o->n + 1 < o->max) o->s[o->n++] = c;
    else o->full = 1;
}

static void put_str(out_t *o, const char *str, size_t len, int width, char pad, int upper)
{
    if (pad != '-')
        for (int k = (int)len; k < width; k++) put(o, ' ');
    for (size_t k = 0; k < len; k++) {
        char c = str[k];
        if (upper && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        put(o, c);
    }
}

static void put_num(out_t *o, long v, int width, char pad)
{
    char buf[24];
    int len = 0, neg = v < 0;
    unsigned long u = neg ? (unsigned long)-v : (unsigned long)v;
    do { buf[len++] = (char)('0' + u % 10); u /= 10; } while (u);
    int digits = len + neg;
    if (pad == '0' && neg) put(o, '-');
    if (pad != '-')
        for (int k = digits; k < width; k++) put(o, pad);
    if (pad != '0' && neg) put(o, '-');
    while (len) put(o, buf[--len]);
}

static int weeks_in_year(int y)
{
    int p  = (y + y / 4 - y / 100 + y / 400) % 7;
    int y1 = y - 1;
    int p1 = (y1 + y1 / 4 - y1 / 100 + y1 / 400) % 7;
    return (p == 4 || p1 == 3) ? 53 : 52;
}

static int iso_week(const struct tm *tm, int *iso_year)
{
    int year = tm->tm_year + 1900;
    int isow = tm->tm_wday == 0 ? 7 : tm->tm_wday;
    int week = (tm->tm_yday + 1 - isow + 10) / 7;
    if (week < 1) {
        year--;
        week = weeks_in_year(year);
    } else if (week > weeks_in_year(year)) {
        year++;
        week = 1;
    }
    *iso_year = year;
    return week;
}

static void format(out_t *o, const char *fmt, const struct tm *tm);

static void convert(out_t *o, char conv, char flag, int width, const struct tm *tm)
{
    char numpad = flag ? flag : '0';
    char spcpad = flag ? flag : ' ';
    int upper = flag == '^';
    if (upper) numpad = '0', spcpad = ' ';
    int hour12 = tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12;
    int year = tm->tm_year + 1900;
    int iy;

    switch (conv) {
        case 'a': {
            const char *d = (unsigned)tm->tm_wday < 7 ? g_wday[tm->tm_wday] : "?";
            put_str(o, d, 3, width, spcpad, upper);
            break;
        }
        case 'A': {
            const char *d = (unsigned)tm->tm_wday < 7 ? g_wday[tm->tm_wday] : "?";
            size_t len = 0;
            while (d[len]) len++;
            put_str(o, d, len, width, spcpad, upper);
            break;
        }
        case 'b':
        case 'h': {
            const char *m = (unsigned)tm->tm_mon < 12 ? g_mon[tm->tm_mon] : "?";
            put_str(o, m, 3, width, spcpad, upper);
            break;
        }
        case 'B': {
            const char *m = (unsigned)tm->tm_mon < 12 ? g_mon[tm->tm_mon] : "?";
            size_t len = 0;
            while (m[len]) len++;
            put_str(o, m, len, width, spcpad, upper);
            break;
        }
        case 'c': format(o, "%a %b %e %H:%M:%S %Y", tm); break;
        case 'C': put_num(o, year / 100, width ? width : 2, numpad); break;
        case 'd': put_num(o, tm->tm_mday, width ? width : 2, numpad); break;
        case 'D': format(o, "%m/%d/%y", tm); break;
        case 'e': put_num(o, tm->tm_mday, width ? width : 2, flag ? numpad : ' '); break;
        case 'F': format(o, "%Y-%m-%d", tm); break;
        case 'g': iso_week(tm, &iy); put_num(o, ((iy % 100) + 100) % 100, width ? width : 2, numpad); break;
        case 'G': iso_week(tm, &iy); put_num(o, iy, width ? width : 4, numpad); break;
        case 'H': put_num(o, tm->tm_hour, width ? width : 2, numpad); break;
        case 'I': put_num(o, hour12, width ? width : 2, numpad); break;
        case 'j': put_num(o, tm->tm_yday + 1, width ? width : 3, numpad); break;
        case 'k': put_num(o, tm->tm_hour, width ? width : 2, flag ? numpad : ' '); break;
        case 'l': put_num(o, hour12, width ? width : 2, flag ? numpad : ' '); break;
        case 'm': put_num(o, tm->tm_mon + 1, width ? width : 2, numpad); break;
        case 'M': put_num(o, tm->tm_min, width ? width : 2, numpad); break;
        case 'n': put(o, '\n'); break;
        case 'p': put_str(o, tm->tm_hour < 12 ? "AM" : "PM", 2, width, spcpad, 0); break;
        case 'P': put_str(o, tm->tm_hour < 12 ? "am" : "pm", 2, width, spcpad, upper); break;
        case 'r': format(o, "%I:%M:%S %p", tm); break;
        case 'R': format(o, "%H:%M", tm); break;
        case 's': {
            struct tm copy = *tm;
            put_num(o, (long)mktime(&copy), width, numpad);
            break;
        }
        case 'S': put_num(o, tm->tm_sec, width ? width : 2, numpad); break;
        case 't': put(o, '\t'); break;
        case 'T': format(o, "%H:%M:%S", tm); break;
        case 'u': put_num(o, tm->tm_wday == 0 ? 7 : tm->tm_wday, width, numpad); break;
        case 'U': put_num(o, (tm->tm_yday + 7 - tm->tm_wday) / 7, width ? width : 2, numpad); break;
        case 'V': put_num(o, iso_week(tm, &iy), width ? width : 2, numpad); break;
        case 'w': put_num(o, tm->tm_wday, width, numpad); break;
        case 'W': put_num(o, (tm->tm_yday + 7 - (tm->tm_wday + 6) % 7) / 7, width ? width : 2, numpad); break;
        case 'x': format(o, "%m/%d/%y", tm); break;
        case 'X': format(o, "%H:%M:%S", tm); break;
        case 'y': put_num(o, ((year % 100) + 100) % 100, width ? width : 2, numpad); break;
        case 'Y': put_num(o, year, width ? width : (year >= 0 ? 4 : 0), numpad); break;
        case 'z': {
            long off = timezone_offset();
            put(o, off < 0 ? '-' : '+');
            if (off < 0) off = -off;
            put_num(o, (off / 3600) * 100 + (off / 60) % 60, 4, '0');
            break;
        }
        case 'Z': {
            const char *z = timezone_name();
            size_t len = 0;
            while (z && z[len]) len++;
            put_str(o, z ? z : "", len, width, spcpad, upper);
            break;
        }
        case '%': put(o, '%'); break;
        default:
            put(o, '%');
            if (conv) put(o, conv);
            break;
    }
}

static void format(out_t *o, const char *fmt, const struct tm *tm)
{
    while (*fmt) {
        if (*fmt != '%') { put(o, *fmt++); continue; }
        fmt++;
        char flag = 0;
        while (*fmt == '-' || *fmt == '_' || *fmt == '0' || *fmt == '^' || *fmt == '#') {
            if (*fmt == '-') flag = '-';
            else if (*fmt == '_') flag = ' ';
            else if (*fmt == '0') flag = '0';
            else if (*fmt == '^') flag = '^';
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == 'E' || *fmt == 'O') fmt++;
        char conv = *fmt;
        if (conv) fmt++;
        convert(o, conv, flag, width, tm);
    }
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    if (!s || !fmt || !tm || max == 0) return 0;
    out_t o = { s, max, 0, 0 };
    format(&o, fmt, tm);
    s[o.n] = '\0';
    return o.full ? 0 : o.n;
}
