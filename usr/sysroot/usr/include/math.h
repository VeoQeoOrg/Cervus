#ifndef _MATH_H
#define _MATH_H

#include <stdint.h>

#define INFINITY (1.0/0.0)
#define NAN (0.0/0.0)

int abs(int x);
double fabs(double x);
double pow(double base, double exp);
double pow10(int n);
int isinf(double x);
int isnan(double x);

double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double log(double x);
double log10(double x);
double exp(double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double fmod(double x, double y);
double modf(double x, double *iptr);
double frexp(double x, int *exp);

static inline double ldexp(double x, int exp) {
    union { double d; uint64_t u; } v;
    v.d = x;
    int e = (int)((v.u >> 52) & 0x7FF);
    if (e == 0 || e == 0x7FF)
        return x;
    e += exp;
    if (e <= 0)
        return 0.0;
    if (e >= 0x7FF)
        return INFINITY;
    v.u = (v.u & ~((uint64_t)0x7FF << 52)) | ((uint64_t)e << 52);
    return v.d;
}

static inline double floor(double x) {
    int64_t i = (int64_t)x;
    return (double)(i - (x < (double)i));
}

static inline double ceil(double x) {
    int64_t i = (int64_t)x;
    return (double)(i + (x > (double)i));
}

static inline double round(double x) {
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

static inline double sqrt(double x) {
    double result;
    asm volatile ("sqrtsd %1, %0" : "=x"(result) : "x"(x));
    return result;
}

static inline float sqrtf(float x) {
    float result;
    asm volatile ("sqrtss %1, %0" : "=x"(result) : "x"(x));
    return result;
}

static inline double log2(double x) {
    double result;
    asm volatile (
        "fld1\n\t"
        "fld  %1\n\t"
        "fyl2x\n\t"
        "fstp %0\n\t"
        : "=m"(result) : "m"(x)
    );
    return result;
}

#define MIN(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a : _b; })
#define MAX(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a : _b; })

#define ALIGN_UP(x, align)   (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

#define IS_POWER_OF_TWO(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))

#endif