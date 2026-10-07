#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

static void usage(void)
{
    fputs("usage: date [-u] [-d @SECONDS | -r FILE] [+FORMAT]\n"
          "       date [-u] MMDDhhmm[[CC]YY][.ss]\n", stderr);
    exit(1);
}

static int two(const char *s)
{
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1])) return -1;
    return (s[0] - '0') * 10 + (s[1] - '0');
}

static int set_clock(const char *arg)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%s", arg);
    int ss = 0;
    char *dot = strchr(buf, '.');
    if (dot) {
        *dot = 0;
        if (strlen(dot + 1) != 2 || (ss = two(dot + 1)) < 0 || ss > 60) return -1;
    }
    size_t n = strlen(buf);
    if (n != 8 && n != 10 && n != 12) return -1;
    for (size_t i = 0; i < n; i++) if (!isdigit((unsigned char)buf[i])) return -1;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int mon = two(buf), mday = two(buf + 2), hour = two(buf + 4), min = two(buf + 6);
    if (n == 12) {
        tm.tm_year = two(buf + 8) * 100 + two(buf + 10) - 1900;
    } else if (n == 10) {
        int yy = two(buf + 8);
        tm.tm_year = (yy < 69 ? 2000 + yy : 1900 + yy) - 1900;
    }
    if (mon < 1 || mon > 12 || mday < 1 || mday > 31 || hour > 23 || min > 59) return -1;
    tm.tm_mon = mon - 1;
    tm.tm_mday = mday;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = ss;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    if (t == (time_t)-1) return -1;
    struct timespec ts = { t, 0 };
    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        fprintf(stderr, "date: cannot set the clock: %s\n", strerror(errno));
        exit(1);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *fmt = "%a %b %e %H:%M:%S %Z %Y";
    const char *when = NULL, *ref = NULL;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (!strcmp(argv[i], "-u")) { setenv("TZ", "UTC0", 1); continue; }
        if (!strcmp(argv[i], "-d") && i + 1 < argc) { when = argv[++i]; continue; }
        if (!strcmp(argv[i], "-r") && i + 1 < argc) { ref = argv[++i]; continue; }
        usage();
    }
    tzset();

    time_t t = time(NULL);
    if (when) {
        char *end;
        if (when[0] != '@') { fprintf(stderr, "date: unsupported date '%s'\n", when); return 1; }
        long long v = strtoll(when + 1, &end, 10);
        if (end == when + 1 || *end) { fprintf(stderr, "date: invalid date '%s'\n", when); return 1; }
        t = (time_t)v;
    } else if (ref) {
        struct stat st;
        if (stat(ref, &st) != 0) { fprintf(stderr, "date: %s: %s\n", ref, strerror(errno)); return 1; }
        t = st.st_mtime;
    }

    for (; i < argc; i++) {
        if (argv[i][0] == '+') { fmt = argv[i] + 1; continue; }
        if (when || ref || i + 1 != argc) usage();
        if (set_clock(argv[i]) != 0) { fprintf(stderr, "date: invalid date '%s'\n", argv[i]); return 1; }
        t = time(NULL);
    }

    struct tm tm;
    if (!localtime_r(&t, &tm)) { fprintf(stderr, "date: time out of range\n"); return 1; }
    char out[1024];
    size_t n = strftime(out, sizeof out, fmt, &tm);
    if (n == 0 && fmt[0]) { fprintf(stderr, "date: format too long\n"); return 1; }
    fwrite(out, 1, n, stdout);
    putchar('\n');
    return 0;
}
