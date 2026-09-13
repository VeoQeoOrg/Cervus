#ifndef _MATH_H
#define _MATH_H

#include <stdint.h>

#define INFINITY (1.0/0.0)
#define NAN (0.0/0.0)

#define M_PI 3.14159265358979323846

int abs(int x);
double fabs(double x);
double pow(double base, double exp);
double pow10(int n);
int isinf(double x);
int isnan(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double atan2(double y, double x);
double atan(double x);
double asin(double x);
double acos(double x);
double exp(double x);
double exp2(double x);
double log(double x);
double log10(double x);
double fmod(double x, double y);
double copysign(double x, double y);
double trunc(double x);
double fmin(double a, double b);
double fmax(double a, double b);
double hypot(double x, double y);
double cbrt(double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);

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

static inline int __m_small(double x) {
    return x > -9.007199254740992e15 && x < 9.007199254740992e15;
}

static inline double floor(double x) {
    if (!__m_small(x)) return x;
    int64_t i = (int64_t)x;
    return (double)(i - (x < (double)i));
}

static inline double ceil(double x) {
    if (!__m_small(x)) return x;
    int64_t i = (int64_t)x;
    return (double)(i + (x > (double)i));
}

static inline double round(double x) {
    if (!__m_small(x)) return x;
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

double frexp(double x, int *e);
double modf(double x, double *ip);
double scalbn(double x, int n);
double rint(double x);
double nearbyint(double x);
long      lround(double x);
long long llround(double x);
long long llrint(double x);
double fdim(double x, double y);
double fma(double x, double y, double z);
double log1p(double x);
double expm1(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);
double remainder(double x, double y);
double nan(const char *tag);

float powf(float x, float y);
float expf(float x);
float exp2f(float x);
float logf(float x);
float log2f(float x);
float log10f(float x);
float log1pf(float x);
float expm1f(float x);
float tanf(float x);
float asinf(float x);
float acosf(float x);
float atanf(float x);
float atan2f(float y, float x);
float sinhf(float x);
float coshf(float x);
float tanhf(float x);
float asinhf(float x);
float acoshf(float x);
float atanhf(float x);
float fmodf(float x, float y);
float roundf(float x);
float truncf(float x);
float rintf(float x);
float nearbyintf(float x);
float copysignf(float x, float y);
float fminf(float a, float b);
float fmaxf(float a, float b);
float fdimf(float a, float b);
float hypotf(float x, float y);
float cbrtf(float x);
float fmaf(float x, float y, float z);
float ldexpf(float x, int e);
float scalbnf(float x, int e);
float frexpf(float x, int *e);
float modff(float x, float *ip);

#define signbit(x)   ((x) < 0 || (1.0 / (x)) < 0)
#define isfinite(x)  (!isnan(x) && !isinf(x))
#define isnormal(x)  (isfinite(x) && (x) != 0)
#define HUGE_VAL     INFINITY
#define HUGE_VALF    ((float)INFINITY)
#define M_E          2.7182818284590452354
#define M_LN2        0.69314718055994530942
#define M_LN10       2.30258509299404568402
#define M_SQRT2      1.41421356237309504880
#define M_PI_2       1.57079632679489661923
#define M_PI_4       0.78539816339744830962

static inline float  fabsf(float x)  { return x < 0 ? -x : x; }
static inline float  floorf(float x) { return (float)floor((double)x); }
static inline float  ceilf(float x)  { return (float)ceil((double)x); }
static inline float  cosf(float x)   { return (float)cos((double)x); }
static inline float  sinf(float x)   { return (float)sin((double)x); }
static inline long   lrint(double x) { return (long)round(x); }
static inline long   lrintf(float x) { return (long)round((double)x); }

#define MIN(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a : _b; })
#define MAX(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a : _b; })

#define ALIGN_UP(x, align)   (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

#define IS_POWER_OF_TWO(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))

#endif