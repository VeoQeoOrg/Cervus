#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libcervus.h>

static const char *prog_name(void) {
    if (__cervus_argc > 0 && __cervus_argv && __cervus_argv[0]) {
        const char *p = __cervus_argv[0];
        const char *s = strrchr(p, '/');
        return s ? s + 1 : p;
    }
    return "?";
}

static void emit_prefix(void) {
    const char *n = prog_name();
    fputs(n, stderr);
    fputs(": ", stderr);
}

void vwarnx(const char *fmt, va_list ap) {
    emit_prefix();
    if (fmt) vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void vwarn(const char *fmt, va_list ap) {
    int e = __cervus_errno;
    emit_prefix();
    if (fmt) {
        vfprintf(stderr, fmt, ap);
        fputs(": ", stderr);
    }
    fputs(strerror(e), stderr);
    fputc('\n', stderr);
}

void warnx(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwarnx(fmt, ap);
    va_end(ap);
}

void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwarn(fmt, ap);
    va_end(ap);
}

void verr(int eval, const char *fmt, va_list ap) {
    vwarn(fmt, ap);
    exit(eval);
}
void verrx(int eval, const char *fmt, va_list ap) {
    vwarnx(fmt, ap);
    exit(eval);
}
void err(int eval, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    verr(eval, fmt, ap);
}
void errx(int eval, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    verrx(eval, fmt, ap);
}
