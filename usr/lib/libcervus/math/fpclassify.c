#include <math.h>
#include <stdint.h>

int __fpclassifyd(double x)
{
    union { double d; uint64_t u; } v = { .d = x };
    uint64_t exp  = (v.u >> 52) & 0x7FF;
    uint64_t frac = v.u & 0xFFFFFFFFFFFFFULL;

    if (exp == 0x7FF) return frac ? FP_NAN : FP_INFINITE;
    if (exp == 0)     return frac ? FP_SUBNORMAL : FP_ZERO;
    return FP_NORMAL;
}

int __fpclassifyf(float x)
{
    union { float f; uint32_t u; } v = { .f = x };
    uint32_t exp  = (v.u >> 23) & 0xFF;
    uint32_t frac = v.u & 0x7FFFFF;

    if (exp == 0xFF) return frac ? FP_NAN : FP_INFINITE;
    if (exp == 0)    return frac ? FP_SUBNORMAL : FP_ZERO;
    return FP_NORMAL;
}

int __signbitd(double x)
{
    union { double d; uint64_t u; } v = { .d = x };
    return (int)(v.u >> 63);
}

int __signbitf(float x)
{
    union { float f; uint32_t u; } v = { .f = x };
    return (int)(v.u >> 31);
}
