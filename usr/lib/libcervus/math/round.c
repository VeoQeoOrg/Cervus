#include <math.h>
#include <stdint.h>

static int m_small(double x)
{
    return x > -9.007199254740992e15 && x < 9.007199254740992e15;
}

double floor(double x)
{
    if (!m_small(x)) return x;
    int64_t i = (int64_t)x;
    return (double)(i - (x < (double)i));
}

double ceil(double x)
{
    if (!m_small(x)) return x;
    int64_t i = (int64_t)x;
    return (double)(i + (x > (double)i));
}

double round(double x)
{
    if (!m_small(x)) return x;
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

double sqrt(double x)
{
    double r;
    __asm__ volatile ("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}

float floorf(float x) { return (float)floor((double)x); }
float ceilf(float x)  { return (float)ceil((double)x); }
float sqrtf(float x)  { return (float)sqrt((double)x); }
