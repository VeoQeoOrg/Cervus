#include <math.h>
#include <stdint.h>

int isinf(double x) {
    union {
        double d;
        struct {
            uint64_t mantissa : 52;
            uint32_t exponent : 11;
            uint32_t sign : 1;
        } bits;
    } u = { .d = x };
    
    // Бесконечность: все биты экспоненты = 1, мантисса = 0
    return (u.bits.exponent == 0x7FF) && (u.bits.mantissa == 0);
}