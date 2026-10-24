#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "darwin.h"

static const unsigned char lx2d[] = {
    [EAGAIN] = 35, [EDEADLK] = 11, [EINPROGRESS] = 36, [EALREADY] = 37,
    [ENOTSOCK] = 38, [EDESTADDRREQ] = 39, [EMSGSIZE] = 40, [EPROTOTYPE] = 41,
    [ENOPROTOOPT] = 42, [EPROTONOSUPPORT] = 43, [ESOCKTNOSUPPORT] = 44,
    [EOPNOTSUPP] = 45, [EPFNOSUPPORT] = 46, [EAFNOSUPPORT] = 47,
    [EADDRINUSE] = 48, [EADDRNOTAVAIL] = 49, [ENETDOWN] = 50,
    [ENETUNREACH] = 51, [ENETRESET] = 52, [ECONNABORTED] = 53,
    [ECONNRESET] = 54, [ENOBUFS] = 55, [EISCONN] = 56, [ENOTCONN] = 57,
    [ESHUTDOWN] = 58, [ETOOMANYREFS] = 59, [ETIMEDOUT] = 60,
    [ECONNREFUSED] = 61, [ELOOP] = 62, [ENAMETOOLONG] = 63, [EHOSTDOWN] = 64,
    [EHOSTUNREACH] = 65, [ENOTEMPTY] = 66, [EUSERS] = 68, [EDQUOT] = 69,
    [ESTALE] = 70, [EREMOTE] = 71, [ENOLCK] = 77, [ENOSYS] = 78,
    [EOVERFLOW] = 84, [ECANCELED] = 89, [EIDRM] = 90, [ENOMSG] = 91,
    [EILSEQ] = 92, [EBADMSG] = 94, [EMULTIHOP] = 95, [ENODATA] = 96,
    [ENOLINK] = 97, [ENOSR] = 98, [ENOSTR] = 99, [EPROTO] = 100, [ETIME] = 101,
    [ENOTRECOVERABLE] = 104, [EOWNERDEAD] = 105,
};

static unsigned char d2lx[128];

int d_errno_lx2d(int e)
{
    if (e > 0 && e < (int)sizeof lx2d && lx2d[e])
        return lx2d[e];
    if (e > 0 && e <= 34)
        return e;
    return e ? 22 : 0;
}

int d_errno_d2lx(int e)
{
    if (!d2lx[1]) {
        for (int i = 1; i <= 34; i++)
            d2lx[i] = (unsigned char)i;
        for (int i = 1; i < (int)sizeof lx2d; i++)
            if (lx2d[i])
                d2lx[lx2d[i]] = (unsigned char)i;
        d2lx[102] = EOPNOTSUPP;
    }
    if (e > 0 && e < (int)sizeof d2lx && d2lx[e])
        return d2lx[e];
    return e ? EINVAL : 0;
}

static __thread int d_errval;
extern void (*__cervus_errno_hook)(int);

static void errno_hook(int e)
{
    d_errval = d_errno_lx2d(e);
}

void d_errno_init(void)
{
    __cervus_errno_hook = errno_hook;
}

int *d___error(void)
{
    int e = errno;
    if (e) {
        d_errval = d_errno_lx2d(e);
        errno = 0;
    }
    return &d_errval;
}
DSYM("___error", d___error);

static int cur_lx_errno(void)
{
    return d_errno_d2lx(*d___error());
}

static char *d_strerror(int n)
{
    return strerror(d_errno_d2lx(n));
}
DSYM("_strerror", d_strerror);

static int d_strerror_r(int n, char *buf, size_t len)
{
    int r = strerror_r(d_errno_d2lx(n), buf, len);
    return r ? d_errno_lx2d(r) : 0;
}
DSYM("_strerror_r", d_strerror_r);

static void d_perror(const char *s)
{
    const char *m = strerror(cur_lx_errno());
    fflush(stdout);
    if (s && *s)
        fprintf(stderr, "%s: %s\n", s, m);
    else
        fprintf(stderr, "%s\n", m);
}
DSYM("_perror", d_perror);

static FILE *g_err_file;
static void (*g_err_exit)(int);

static void d_err_set_file(void *f)
{
    g_err_file = f ? d_real_file(f) : NULL;
}
DSYM("_err_set_file", d_err_set_file);

static void d_err_set_exit(void (*fn)(int))
{
    g_err_exit = fn;
}
DSYM("_err_set_exit", d_err_set_exit);

static void report(int lx, const char *fmt, va_list ap)
{
    FILE *f = g_err_file ? g_err_file : stderr;
    fflush(stdout);
    fprintf(f, "%s: ", d_progname);
    if (fmt) {
        vfprintf(f, fmt, ap);
        if (lx >= 0)
            fputs(": ", f);
    }
    if (lx >= 0)
        fputs(strerror(lx), f);
    fputc('\n', f);
}

static void finish(int code)
{
    if (g_err_exit)
        g_err_exit(code);
    exit(code);
}

static void d_vwarn(const char *fmt, va_list ap) { report(cur_lx_errno(), fmt, ap); }
static void d_vwarnx(const char *fmt, va_list ap) { report(-1, fmt, ap); }
static void d_vwarnc(int code, const char *fmt, va_list ap) { report(d_errno_d2lx(code), fmt, ap); }
static void d_verr(int ev, const char *fmt, va_list ap) { report(cur_lx_errno(), fmt, ap); finish(ev); }
static void d_verrx(int ev, const char *fmt, va_list ap) { report(-1, fmt, ap); finish(ev); }
static void d_verrc(int ev, int code, const char *fmt, va_list ap) { report(d_errno_d2lx(code), fmt, ap); finish(ev); }

static void d_warn(const char *fmt, ...) { va_list ap; va_start(ap, fmt); d_vwarn(fmt, ap); va_end(ap); }
static void d_warnx(const char *fmt, ...) { va_list ap; va_start(ap, fmt); d_vwarnx(fmt, ap); va_end(ap); }
static void d_warnc(int code, const char *fmt, ...) { va_list ap; va_start(ap, fmt); d_vwarnc(code, fmt, ap); va_end(ap); }
static void d_err(int ev, const char *fmt, ...) { va_list ap; va_start(ap, fmt); d_verr(ev, fmt, ap); va_end(ap); }
static void d_errx(int ev, const char *fmt, ...) { va_list ap; va_start(ap, fmt); d_verrx(ev, fmt, ap); va_end(ap); }
static void d_errc(int ev, int code, const char *fmt, ...) { va_list ap; va_start(ap, fmt); d_verrc(ev, code, fmt, ap); va_end(ap); }

DSYM("_vwarn", d_vwarn);
DSYM("_vwarnx", d_vwarnx);
DSYM("_vwarnc", d_vwarnc);
DSYM("_verr", d_verr);
DSYM("_verrx", d_verrx);
DSYM("_verrc", d_verrc);
DSYM("_warn", d_warn);
DSYM("_warnx", d_warnx);
DSYM("_warnc", d_warnc);
DSYM("_err", d_err);
DSYM("_errx", d_errx);
DSYM("_errc", d_errc);
