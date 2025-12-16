#include <math.h>
#include <stdint.h>

int isnan(double x) {
    union {
        double d;
        struct {
            uint64_t mantissa : 52;
            uint32_t exponent : 11;
            uint32_t sign : 1;
        } bits;
    } u = { .d = x };
    
    return (u.bits.exponent == 0x7FF) && (u.bits.mantissa != 0);
}