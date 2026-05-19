#ifndef _ERR_H
#define _ERR_H

#include <stdarg.h>

void err(int eval, const char *fmt, ...) __attribute__((noreturn));
void errx(int eval, const char *fmt, ...) __attribute__((noreturn));
void warn(const char *fmt, ...);
void warnx(const char *fmt, ...);
void verr(int eval, const char *fmt, va_list ap) __attribute__((noreturn));
void verrx(int eval, const char *fmt, va_list ap) __attribute__((noreturn));
void vwarn(const char *fmt, va_list ap);
void vwarnx(const char *fmt, va_list ap);

#endif
