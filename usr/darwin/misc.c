#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <getopt.h>
#include <limits.h>
#include <malloc.h>
#include <sys/auxv.h>
#include <sys/random.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <wchar.h>
#include <fcntl.h>
#include <setjmp.h>
#include <fnmatch.h>
#include <glob.h>
#include <regex.h>
#include <search.h>
#include <ftw.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <sys/file.h>
#include "darwin.h"

extern char **environ;
extern int __cervus_optpos;

static uintptr_t d_stack_chk_guard = 0x2f8a4c1d9e3b7056ull;
DSYM("___stack_chk_guard", &d_stack_chk_guard);

void d_misc_init(void)
{
    const uint8_t *r = (const uint8_t *)getauxval(AT_RANDOM);
    if (r)
        memcpy(&d_stack_chk_guard, r + 8, sizeof d_stack_chk_guard);
    d_stack_chk_guard &= ~(uintptr_t)0xff;
}

static void chk_fail(const char *what)
{
    fflush(stdout);
    fprintf(stderr, "%s: %s\n", d_progname, what);
    abort();
}

static void d___stack_chk_fail(void) { chk_fail("stack buffer overflow detected"); }
static void d___chk_fail_overflow(void) { chk_fail("buffer overflow detected"); }
DSYM("___stack_chk_fail", d___stack_chk_fail);
DSYM("___chk_fail_overflow", d___chk_fail_overflow);
DSYM("___chk_fail_overlap", d___chk_fail_overflow);

__asm__(
    ".text\n"
    ".globl d_chkstk_darwin\n"
    "d_chkstk_darwin:\n"
    "    push %rcx\n"
    "    push %rax\n"
    "    lea 24(%rsp), %rcx\n"
    "1:  cmp $0x1000, %rax\n"
    "    jb 2f\n"
    "    sub $0x1000, %rcx\n"
    "    test %rcx, (%rcx)\n"
    "    sub $0x1000, %rax\n"
    "    jmp 1b\n"
    "2:  sub %rax, %rcx\n"
    "    test %rcx, (%rcx)\n"
    "    pop %rax\n"
    "    pop %rcx\n"
    "    ret\n");
void d_chkstk_darwin(void);
DSYM("____chkstk_darwin", d_chkstk_darwin);

static int d___vsprintf_chk(char *s, int flag, size_t slen, const char *fmt, va_list ap)
{
    (void)flag;
    if (slen == (size_t)-1)
        return vsprintf(s, fmt, ap);
    int r = vsnprintf(s, slen, fmt, ap);
    if (r >= 0 && (size_t)r >= slen)
        chk_fail("buffer overflow detected");
    return r;
}

static int d___sprintf_chk(char *s, int flag, size_t slen, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = d___vsprintf_chk(s, flag, slen, fmt, ap);
    va_end(ap);
    return r;
}

static int d___vsnprintf_chk(char *s, size_t max, int flag, size_t slen, const char *fmt, va_list ap)
{
    (void)flag;
    if (max > slen)
        chk_fail("buffer overflow detected");
    return vsnprintf(s, max, fmt, ap);
}

static int d___snprintf_chk(char *s, size_t max, int flag, size_t slen, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = d___vsnprintf_chk(s, max, flag, slen, fmt, ap);
    va_end(ap);
    return r;
}

static char *d___strcpy_chk(char *d, const char *s, size_t dl)
{
    size_t n = strlen(s);
    if (n >= dl)
        chk_fail("buffer overflow detected");
    return memcpy(d, s, n + 1);
}

static char *d___stpcpy_chk(char *d, const char *s, size_t dl)
{
    size_t n = strlen(s);
    if (n >= dl)
        chk_fail("buffer overflow detected");
    memcpy(d, s, n + 1);
    return d + n;
}

static char *d___strcat_chk(char *d, const char *s, size_t dl)
{
    size_t a = strlen(d), b = strlen(s);
    if (a + b >= dl)
        chk_fail("buffer overflow detected");
    memcpy(d + a, s, b + 1);
    return d;
}

static char *d___strncpy_chk(char *d, const char *s, size_t n, size_t dl)
{
    if (n > dl)
        chk_fail("buffer overflow detected");
    return strncpy(d, s, n);
}

static char *d___stpncpy_chk(char *d, const char *s, size_t n, size_t dl)
{
    if (n > dl)
        chk_fail("buffer overflow detected");
    return stpncpy(d, s, n);
}

static char *d___strncat_chk(char *d, const char *s, size_t n, size_t dl)
{
    size_t a = strlen(d), b = strnlen(s, n);
    if (a + b >= dl)
        chk_fail("buffer overflow detected");
    memcpy(d + a, s, b);
    d[a + b] = 0;
    return d;
}

static size_t d___strlcpy_chk(char *d, const char *s, size_t n, size_t dl)
{
    if (n > dl)
        chk_fail("buffer overflow detected");
    return strlcpy(d, s, n);
}

static size_t d___strlcat_chk(char *d, const char *s, size_t n, size_t dl)
{
    if (n > dl)
        chk_fail("buffer overflow detected");
    return strlcat(d, s, n);
}

static void *d___memcpy_chk(void *d, const void *s, size_t n, size_t dl)
{
    if (n > dl)
        chk_fail("buffer overflow detected");
    return memcpy(d, s, n);
}

static void *d___memmove_chk(void *d, const void *s, size_t n, size_t dl)
{
    if (n > dl)
        chk_fail("buffer overflow detected");
    return memmove(d, s, n);
}

static void *d___memset_chk(void *d, int c, size_t n, size_t dl)
{
    if (n > dl)
        chk_fail("buffer overflow detected");
    return memset(d, c, n);
}

static void *d_memccpy(void *d, const void *s, int c, size_t n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;
    for (size_t i = 0; i < n; i++) {
        dp[i] = sp[i];
        if (sp[i] == (uint8_t)c)
            return dp + i + 1;
    }
    return NULL;
}

static void *d___memccpy_chk(void *d, const void *s, int c, size_t n, size_t dl)
{
    if (n > dl)
        chk_fail("buffer overflow detected");
    return d_memccpy(d, s, c, n);
}

DSYM("___sprintf_chk", d___sprintf_chk);
DSYM("___vsprintf_chk", d___vsprintf_chk);
DSYM("___snprintf_chk", d___snprintf_chk);
DSYM("___vsnprintf_chk", d___vsnprintf_chk);
DSYM("___strcpy_chk", d___strcpy_chk);
DSYM("___stpcpy_chk", d___stpcpy_chk);
DSYM("___strcat_chk", d___strcat_chk);
DSYM("___strncpy_chk", d___strncpy_chk);
DSYM("___stpncpy_chk", d___stpncpy_chk);
DSYM("___strncat_chk", d___strncat_chk);
DSYM("___strlcpy_chk", d___strlcpy_chk);
DSYM("___strlcat_chk", d___strlcat_chk);
DSYM("___memcpy_chk", d___memcpy_chk);
DSYM("___memmove_chk", d___memmove_chk);
DSYM("___memset_chk", d___memset_chk);
DSYM("___memccpy_chk", d___memccpy_chk);

static void d_bzero(void *p, size_t n) { memset(p, 0, n); }
static void d_bcopy(const void *s, void *d, size_t n) { memmove(d, s, n); }
static int d_bcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n) != 0; }
static void d_memset_pattern(void *b, const void *pat, size_t len, size_t pl)
{
    uint8_t *p = b;
    for (size_t i = 0; i < len; i++)
        p[i] = ((const uint8_t *)pat)[i % pl];
}
static void d_memset_pattern4(void *b, const void *p, size_t n) { d_memset_pattern(b, p, n, 4); }
static void d_memset_pattern8(void *b, const void *p, size_t n) { d_memset_pattern(b, p, n, 8); }
static void d_memset_pattern16(void *b, const void *p, size_t n) { d_memset_pattern(b, p, n, 16); }

DSYM("___bzero", d_bzero);
DSYM("_bzero", d_bzero);
DSYM("_bcopy", d_bcopy);
DSYM("_bcmp", d_bcmp);
DSYM("_memset_pattern4", d_memset_pattern4);
DSYM("_memset_pattern8", d_memset_pattern8);
DSYM("_memset_pattern16", d_memset_pattern16);

static int *d_NSGetArgc(void) { return &d_argc; }
static char ***d_NSGetArgv(void) { return &d_argv; }
static char ***d_NSGetEnviron(void) { return &environ; }
static const char **d_NSGetProgname(void) { return &d_progname; }
static void *d_NSGetMachExecuteHeader(void) { return (void *)d_mh; }

static int d_NSGetExecutablePath(char *buf, uint32_t *size)
{
    size_t n = strlen(d_exec_path) + 1;
    if (n > *size) {
        *size = (uint32_t)n;
        return -1;
    }
    memcpy(buf, d_exec_path, n);
    return 0;
}

static const char *d_getprogname(void) { return d_progname; }

static void d_setprogname(const char *p)
{
    const char *s = strrchr(p, '/');
    d_progname = s ? s + 1 : p;
}

DSYM("__NSGetArgc", d_NSGetArgc);
DSYM("__NSGetArgv", d_NSGetArgv);
DSYM("__NSGetEnviron", d_NSGetEnviron);
DSYM("__NSGetProgname", d_NSGetProgname);
DSYM("__NSGetMachExecuteHeader", d_NSGetMachExecuteHeader);
DSYM("__NSGetExecutablePath", d_NSGetExecutablePath);
DSYM("_getprogname", d_getprogname);
DSYM("_setprogname", d_setprogname);
DSYM("_environ", &environ);
DSYM("___progname", &d_progname);

struct dpasswd {
    char *pw_name;
    char *pw_passwd;
    uint32_t pw_uid;
    uint32_t pw_gid;
    long pw_change;
    char *pw_class;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
    long pw_expire;
};

static char g_empty[] = "";

static struct dpasswd *conv_pw(const struct passwd *p, struct dpasswd *d)
{
    if (!p)
        return NULL;
    d->pw_name = p->pw_name;
    d->pw_passwd = p->pw_passwd;
    d->pw_uid = p->pw_uid;
    d->pw_gid = p->pw_gid;
    d->pw_change = 0;
    d->pw_class = g_empty;
    d->pw_gecos = p->pw_gecos;
    d->pw_dir = p->pw_dir;
    d->pw_shell = p->pw_shell;
    d->pw_expire = 0;
    return d;
}

static struct dpasswd g_pw;
static struct dpasswd *d_getpwuid(uid_t u) { return conv_pw(getpwuid(u), &g_pw); }
static struct dpasswd *d_getpwnam(const char *n) { return conv_pw(getpwnam(n), &g_pw); }
static struct dpasswd *d_getpwent(void) { return conv_pw(getpwent(), &g_pw); }

static int d_getpwuid_r(uid_t u, struct dpasswd *pw, char *buf, size_t len, struct dpasswd **res)
{
    struct passwd p, *r = NULL;
    int e = getpwuid_r(u, &p, buf, len, &r);
    *res = r ? conv_pw(&p, pw) : NULL;
    return e ? d_errno_lx2d(e) : 0;
}

static int d_getpwnam_r(const char *n, struct dpasswd *pw, char *buf, size_t len, struct dpasswd **res)
{
    struct passwd p, *r = NULL;
    int e = getpwnam_r(n, &p, buf, len, &r);
    *res = r ? conv_pw(&p, pw) : NULL;
    return e ? d_errno_lx2d(e) : 0;
}

DSYM("_getpwuid", d_getpwuid);
DSYM("_getpwnam", d_getpwnam);
DSYM("_getpwent", d_getpwent);
DSYM("_getpwuid_r", d_getpwuid_r);
DSYM("_getpwnam_r", d_getpwnam_r);
DPASS(setpwent);
DPASS(endpwent);
DPASS(getgrgid);
DPASS(getgrnam);
DPASS(getgrent);
DPASS(setgrent);
DPASS(endgrent);
DPASS(getgrouplist);
DPASS(getgroups);

struct dtm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    char *tm_zone;
};

static char g_utc[] = "UTC";

static void finish_local(struct dtm *d, const struct tm *t, time_t when)
{
    struct tm tmp = *t;
    memcpy(d, t, sizeof *t);
    d->tm_gmtoff = (long)(timegm(&tmp) - when);
    d->tm_zone = tzname[t->tm_isdst > 0];
}

static struct dtm *d_localtime_r(const time_t *w, struct dtm *d)
{
    struct tm t;
    if (!localtime_r(w, &t))
        return NULL;
    finish_local(d, &t, *w);
    return d;
}

static struct dtm *d_gmtime_r(const time_t *w, struct dtm *d)
{
    struct tm t;
    if (!gmtime_r(w, &t))
        return NULL;
    memcpy(d, &t, sizeof t);
    d->tm_gmtoff = 0;
    d->tm_zone = g_utc;
    return d;
}

static struct dtm g_tm;
static struct dtm *d_localtime(const time_t *w) { return d_localtime_r(w, &g_tm); }
static struct dtm *d_gmtime(const time_t *w) { return d_gmtime_r(w, &g_tm); }

static time_t d_mktime(struct dtm *d)
{
    struct tm t;
    memcpy(&t, d, sizeof t);
    time_t r = mktime(&t);
    if (r != (time_t)-1)
        finish_local(d, &t, r);
    return r;
}

static time_t d_timegm(struct dtm *d)
{
    struct tm t;
    memcpy(&t, d, sizeof t);
    time_t r = timegm(&t);
    if (r != (time_t)-1) {
        memcpy(d, &t, sizeof t);
        d->tm_gmtoff = 0;
        d->tm_zone = g_utc;
    }
    return r;
}

static size_t d_strftime(char *s, size_t max, const char *fmt, const struct dtm *d)
{
    struct tm t;
    memcpy(&t, d, sizeof t);
    return strftime(s, max, fmt, &t);
}

static char *d_asctime(const struct dtm *d)
{
    struct tm t;
    memcpy(&t, d, sizeof t);
    return asctime(&t);
}

static char *d_strptime(const char *s, const char *fmt, struct dtm *d)
{
    struct tm t;
    memcpy(&t, d, sizeof t);
    char *r = strptime(s, fmt, &t);
    memcpy(d, &t, sizeof t);
    return r;
}

DSYM("_localtime", d_localtime);
DSYM("_localtime_r", d_localtime_r);
DSYM("_gmtime", d_gmtime);
DSYM("_gmtime_r", d_gmtime_r);
DSYM("_mktime", d_mktime);
DSYM("_timelocal", d_mktime);
DSYM("_timegm", d_timegm);
DSYM("_strftime", d_strftime);
DSYM("_asctime", d_asctime);
DSYM("_strptime", d_strptime);
DPASS(time);
DPASS(ctime);
DPASS(difftime);
DPASS(tzset);
DSYM("_tzname", tzname);

struct dtimeval {
    long tv_sec;
    int tv_usec;
};

static int d_gettimeofday(struct dtimeval *tv, void *tz)
{
    (void)tz;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (tv) {
        tv->tv_sec = ts.tv_sec;
        tv->tv_usec = (int)(ts.tv_nsec / 1000);
    }
    return 0;
}

static clockid_t clock_id(int c)
{
    switch (c) {
    case 0: return CLOCK_REALTIME;
    case 12: return CLOCK_PROCESS_CPUTIME_ID;
    case 16: return CLOCK_THREAD_CPUTIME_ID;
    default: return CLOCK_MONOTONIC;
    }
}

static int d_clock_gettime(int c, struct timespec *ts) { return clock_gettime(clock_id(c), ts); }
static int d_clock_getres(int c, struct timespec *ts) { return clock_getres(clock_id(c), ts); }

static uint64_t d_clock_gettime_nsec_np(int c)
{
    struct timespec ts;
    if (clock_gettime(clock_id(c), &ts) < 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t d_mach_absolute_time(void) { return d_clock_gettime_nsec_np(6); }

static int d_mach_timebase_info(uint32_t *tb)
{
    tb[0] = 1;
    tb[1] = 1;
    return 0;
}

DSYM("_gettimeofday", d_gettimeofday);
DSYM("_clock_gettime", d_clock_gettime);
DSYM("_clock_getres", d_clock_getres);
DSYM("_clock_gettime_nsec_np", d_clock_gettime_nsec_np);
DSYM("_mach_absolute_time", d_mach_absolute_time);
DSYM("_mach_continuous_time", d_mach_absolute_time);
DSYM("_mach_approximate_time", d_mach_absolute_time);
DSYM("_mach_timebase_info", d_mach_timebase_info);
DPASS(nanosleep);
DPASS(sleep);
DPASS(usleep);

static uint32_t d_arc4random(void)
{
    uint32_t v;
    getentropy(&v, sizeof v);
    return v;
}

static void d_arc4random_buf(void *p, size_t n)
{
    uint8_t *b = p;
    while (n) {
        size_t c = n > 256 ? 256 : n;
        getentropy(b, c);
        b += c;
        n -= c;
    }
}

static uint32_t d_arc4random_uniform(uint32_t ub)
{
    if (ub < 2)
        return 0;
    uint32_t min = -ub % ub, r;
    do
        r = d_arc4random();
    while (r < min);
    return r % ub;
}

DSYM("_arc4random", d_arc4random);
DSYM("_arc4random_buf", d_arc4random_buf);
DSYM("_arc4random_uniform", d_arc4random_uniform);

static void *d_reallocf(void *p, size_t n)
{
    void *r = realloc(p, n);
    if (!r && n)
        free(p);
    return r;
}

static size_t d_malloc_size(const void *p) { return p ? malloc_usable_size((void *)p) : 0; }
static size_t d_malloc_good_size(size_t n) { return (n + 15) & ~(size_t)15; }

DSYM("_reallocf", d_reallocf);
DSYM("_malloc_size", d_malloc_size);
DSYM("_malloc_good_size", d_malloc_good_size);
DPASS(malloc);
DPASS(calloc);
DPASS(realloc);
DPASS(free);
DPASS(valloc);
DPASS(posix_memalign);
DPASS(aligned_alloc);

struct qr_ctx {
    void *thunk;
    int (*cmp)(void *, const void *, const void *);
};

static __thread struct qr_ctx *g_qr;

static int qr_cmp(const void *a, const void *b)
{
    return g_qr->cmp(g_qr->thunk, a, b);
}

static void d_qsort_r(void *base, size_t n, size_t sz, void *thunk, int (*cmp)(void *, const void *, const void *))
{
    struct qr_ctx c = { thunk, cmp }, *prev = g_qr;
    g_qr = &c;
    qsort(base, n, sz, qr_cmp);
    g_qr = prev;
}
DSYM("_qsort_r", d_qsort_r);

static int d_mergesort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    if (n < 2)
        return 0;
    uint8_t *a = base, *tmp = malloc(n * sz);
    if (!tmp)
        return -1;
    for (size_t w = 1; w < n; w *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * w) {
            size_t mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                if (cmp(a + j * sz, a + i * sz) < 0)
                    memcpy(tmp + k++ * sz, a + j++ * sz, sz);
                else
                    memcpy(tmp + k++ * sz, a + i++ * sz, sz);
            }
            while (i < mid)
                memcpy(tmp + k++ * sz, a + i++ * sz, sz);
            while (j < hi)
                memcpy(tmp + k++ * sz, a + j++ * sz, sz);
        }
        memcpy(a, tmp, n * sz);
    }
    free(tmp);
    return 0;
}
DSYM("_mergesort", d_mergesort);
DSYM("_heapsort", d_mergesort);

static int d_optreset;
DSYM("_optreset", &d_optreset);
DSYM("_optind", &optind);
DSYM("_optarg", &optarg);
DSYM("_opterr", &opterr);
DSYM("_optopt", &optopt);

static void check_optreset(void)
{
    if (d_optreset) {
        d_optreset = 0;
        __cervus_optpos = 1;
    }
}

static int d_getopt(int argc, char *const argv[], const char *o)
{
    check_optreset();
    return getopt(argc, argv, o);
}

static int d_getopt_long(int argc, char *const argv[], const char *o, const struct option *l, int *idx)
{
    check_optreset();
    return getopt_long(argc, argv, o, l, idx);
}

static int d_getopt_long_only(int argc, char *const argv[], const char *o, const struct option *l, int *idx)
{
    check_optreset();
    return getopt_long_only(argc, argv, o, l, idx);
}

DSYM("_getopt", d_getopt);
DSYM("_getopt_long", d_getopt_long);
DSYM("_getopt_long_only", d_getopt_long_only);

struct dutsname {
    char sysname[256], nodename[256], release[256], version[256], machine[256];
};

static int d_uname(struct dutsname *u)
{
    struct utsname s;
    if (uname(&s) < 0)
        return -1;
    memset(u, 0, sizeof *u);
    strcpy(u->sysname, "Darwin");
    snprintf(u->nodename, sizeof u->nodename, "%s", s.nodename);
    strcpy(u->release, "23.6.0");
    snprintf(u->version, sizeof u->version, "Darwin Kernel Version 23.6.0 (%s %s %s)", s.sysname, s.release, s.version);
    strcpy(u->machine, "x86_64");
    return 0;
}
DSYM("_uname", d_uname);

static long d_sysconf(int n)
{
    switch (n) {
    case 1: return sysconf(_SC_ARG_MAX);
    case 2: return sysconf(_SC_CHILD_MAX);
    case 3: return sysconf(_SC_CLK_TCK);
    case 4: return sysconf(_SC_NGROUPS_MAX);
    case 5: return sysconf(_SC_OPEN_MAX);
    case 6: return 1;
    case 7: return 1;
    case 8: return 200112L;
    case 15: return 2048;
    case 16: return 255;
    case 26: return sysconf(_SC_STREAM_MAX);
    case 27: return 255;
    case 29: return sysconf(_SC_PAGESIZE);
    case 57: return sysconf(_SC_NPROCESSORS_CONF);
    case 58: return sysconf(_SC_NPROCESSORS_ONLN);
    case 70: case 71: return 4096;
    case 72: return 255;
    case 73: return 255;
    case 101: return 255;
    case 120: return 32;
    case 200: return sysconf(_SC_PHYS_PAGES);
    }
    errno = EINVAL;
    return -1;
}
DSYM("_sysconf", d_sysconf);

static int d_getpagesize(void) { return (int)sysconf(_SC_PAGESIZE); }
DSYM("_getpagesize", d_getpagesize);

static int d_getentropy(void *b, size_t n) { return getentropy(b, n); }
DSYM("_getentropy", d_getentropy);

DPASS(exit);
DPASS(_Exit);
DSYM("__exit", _exit);
DPASS(abort);
DPASS(atexit);
DPASS(__cxa_atexit);
DPASS(getenv);
DPASS(setenv);
DPASS(unsetenv);
DPASS(putenv);
DPASS(getpid);
DPASS(getppid);
DPASS(getuid);
DPASS(geteuid);
DPASS(getgid);
DPASS(getegid);
DPASS(setuid);
DPASS(setgid);
DPASS(seteuid);
DPASS(setegid);
DPASS(getpgrp);
DPASS(setpgid);
DPASS(getpgid);
DPASS(setsid);
DPASS(getsid);
DPASS(getlogin);
DPASS(gethostname);
DPASS(fork);
DPASS(execv);
DPASS(execve);
DPASS(execvp);
DPASS(execl);
DPASS(execlp);
DPASS(execle);
DPASS(tcgetpgrp);
DPASS(tcsetpgrp);

DPASS(atoi);
DPASS(atol);
DPASS(atoll);
DPASS(atof);
DPASS(strtol);
DPASS(strtoul);
DPASS(strtoll);
DPASS(strtoull);
DSYM("_strtoq", strtoll);
DSYM("_strtouq", strtoull);
DPASS(strtod);
DPASS(strtof);
DPASS(strtold);
DPASS(strtoimax);
DPASS(strtoumax);
DPASS(abs);
DPASS(labs);
DPASS(llabs);
DPASS(div);
DPASS(ldiv);
DPASS(lldiv);
DPASS(qsort);
DPASS(bsearch);
DPASS(rand);
DPASS(srand);
DPASS(rand_r);
DPASS(random);
DPASS(srandom);
DPASS(drand48);
DPASS(erand48);
DPASS(lrand48);
DPASS(nrand48);
DPASS(mrand48);
DPASS(jrand48);
DPASS(srand48);
DPASS(getsubopt);
DPASS(wcstol);
DPASS(wcstoul);
DPASS(wcstod);

DPASS(memchr);
DPASS(memcmp);
DPASS(memcpy);
DPASS(memmove);
DPASS(memset);
DSYM("_memccpy", d_memccpy);
DPASS(memmem);
DPASS(strcat);
DPASS(strchr);
DPASS(strcmp);
DPASS(strcoll);
DPASS(strcpy);
DPASS(strcspn);
DPASS(strdup);
DPASS(strndup);
DPASS(strlen);
DPASS(strnlen);
DPASS(strncat);
DPASS(strncmp);
DPASS(strncpy);
DPASS(strpbrk);
DPASS(strrchr);
DPASS(strspn);
DPASS(strstr);
DPASS(strtok);
DPASS(strtok_r);
DPASS(strxfrm);
DPASS(strcasecmp);
DPASS(strncasecmp);
DPASS(strcasestr);
DPASS(strsep);
DPASS(strlcpy);
DPASS(strlcat);
DPASS(stpcpy);
DPASS(stpncpy);
DPASS(index);
DPASS(rindex);
DPASS(ffs);
DPASS(wcslen);
DPASS(wcscpy);
DPASS(wcsncpy);
DPASS(wcscat);
DPASS(wcscmp);
DPASS(wcsncmp);
DPASS(wcschr);
DPASS(wcsrchr);
DPASS(wcsstr);
DPASS(wcsdup);
DPASS(wcscoll);
DPASS(wmemchr);
DPASS(wmemcmp);
DPASS(wmemcpy);
DPASS(wmemmove);
DPASS(wmemset);

DPASS(setjmp);
DPASS(longjmp);
DPASS(_setjmp);
DPASS(_longjmp);
DPASS(sigsetjmp);
DPASS(siglongjmp);

static void d___assert_rtn(const char *fn, const char *file, int line, const char *expr)
{
    fflush(stdout);
    if (fn)
        fprintf(stderr, "Assertion failed: (%s), function %s, file %s, line %d.\n", expr, fn, file, line);
    else
        fprintf(stderr, "Assertion failed: (%s), file %s, line %d.\n", expr, file, line);
    abort();
}
DSYM("___assert_rtn", d___assert_rtn);

static void *d_acl_null(void) { errno = EOPNOTSUPP; return NULL; }
static int d_acl_fail(void) { errno = EOPNOTSUPP; return -1; }
static int d_acl_free(void *p) { (void)p; return 0; }
DSYM("_acl_get_file", d_acl_null);
DSYM("_acl_get_fd", d_acl_null);
DSYM("_acl_get_link_np", d_acl_null);
DSYM("_acl_init", d_acl_null);
DSYM("_acl_dup", d_acl_null);
DSYM("_acl_from_text", d_acl_null);
DSYM("_acl_to_text", d_acl_null);
DSYM("_acl_set_file", d_acl_fail);
DSYM("_acl_set_fd", d_acl_fail);
DSYM("_acl_set_link_np", d_acl_fail);
DSYM("_acl_get_entry", d_acl_fail);
DSYM("_acl_free", d_acl_free);

static int d_mkostemp(char *t, int fl)
{
    return mkostemp(t, (fl & 0x1000000) ? O_CLOEXEC : 0);
}

static int d_mkostemps(char *t, int sl, int fl)
{
    return mkostemps(t, sl, (fl & 0x1000000) ? O_CLOEXEC : 0);
}
DSYM("_mkostemp", d_mkostemp);
DSYM("_mkostemps", d_mkostemps);

static __thread char g_pathbuf[PATH_MAX];

static char *d_dirname(const char *p)
{
    if (!p || !*p)
        return strcpy(g_pathbuf, ".");
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/')
        n--;
    while (n > 0 && p[n - 1] != '/')
        n--;
    if (!n)
        return strcpy(g_pathbuf, ".");
    while (n > 1 && p[n - 1] == '/')
        n--;
    if (n >= sizeof g_pathbuf) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    memcpy(g_pathbuf, p, n);
    g_pathbuf[n] = 0;
    return g_pathbuf;
}

static char *d_basename(const char *p)
{
    if (!p || !*p)
        return strcpy(g_pathbuf, ".");
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/')
        n--;
    size_t s = n;
    while (s > 0 && p[s - 1] != '/')
        s--;
    if (n - s >= sizeof g_pathbuf) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    memcpy(g_pathbuf, p + s, n - s);
    g_pathbuf[n - s] = 0;
    return g_pathbuf;
}

static char *d_dirname_r(const char *p, char *buf)
{
    char *r = d_dirname(p);
    return r ? strcpy(buf, r) : NULL;
}

static char *d_basename_r(const char *p, char *buf)
{
    char *r = d_basename(p);
    return r ? strcpy(buf, r) : NULL;
}
DSYM("_dirname", d_dirname);
DSYM("_basename", d_basename);
DSYM("_dirname_r", d_dirname_r);
DSYM("_basename_r", d_basename_r);

static int pc_name(int n)
{
    if (n >= 1 && n <= 9)
        return n - 1;
    return -1;
}

static long d_pathconf(const char *p, int n)
{
    int c = pc_name(n);
    if (c < 0) {
        if (n == 11 || n == 12)
            return 1;
        if (n == 18)
            return 64;
        errno = EINVAL;
        return -1;
    }
    return pathconf(p, c);
}

static long d_fpathconf(int fd, int n)
{
    int c = pc_name(n);
    if (c < 0) {
        if (n == 11 || n == 12)
            return 1;
        if (n == 18)
            return 64;
        errno = EINVAL;
        return -1;
    }
    return fpathconf(fd, c);
}
DSYM("_pathconf", d_pathconf);
DSYM("_fpathconf", d_fpathconf);

static int d_getloadavg(double *l, int n)
{
    FILE *f = fopen("/proc/loadavg", "r");
    double v[3] = { 0, 0, 0 };
    if (f) {
        if (fscanf(f, "%lf %lf %lf", &v[0], &v[1], &v[2]) != 3)
            v[0] = v[1] = v[2] = 0;
        fclose(f);
    }
    if (n > 3)
        n = 3;
    for (int i = 0; i < n; i++)
        l[i] = v[i];
    return n;
}
DSYM("_getloadavg", d_getloadavg);

DPASS(clock);
DPASS(getrusage);
DPASS(times);
DPASS(nice);
DPASS(alarm);
DPASS(pause);
DPASS(getdtablesize);
DPASS(ctermid);
DPASS(ttyname_r);
DPASS(getlogin_r);
DPASS(sethostname);
DPASS(chroot);
DPASS(sync);
DPASS(fdatasync);
DPASS(lockf);
DPASS(confstr);
DPASS(fnmatch);
DPASS(glob);
DPASS(globfree);
DPASS(regcomp);
DPASS(regexec);
DPASS(regerror);
DPASS(regfree);
DPASS(tsearch);
DPASS(tfind);
DPASS(tdelete);
DPASS(twalk);
DPASS(lsearch);
DPASS(lfind);
DPASS(hcreate);
DPASS(hsearch);
DPASS(hdestroy);
DPASS(insque);
DPASS(remque);
DPASS(ftw);
DPASS(nftw);
DPASS(getpass);
DPASS(ffsl);
DPASS(ffsll);
DPASS(a64l);
DPASS(l64a);
DPASS(imaxabs);
DPASS(imaxdiv);
