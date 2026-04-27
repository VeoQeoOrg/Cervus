#include <stdint.h>
#include <stddef.h>

uint64_t __cervus_strtod_bits(const char *s, char **endptr)
{
    if (!s) {
        if (endptr) *endptr = (char *)s;
        return 0;
    }
    const char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' ||
           *p == '\r' || *p == '\f' || *p == '\v') p++;

    int sign = 0;
    if (*p == '+') p++;
    else if (*p == '-') { sign = 1; p++; }

    if ((p[0] == 'i' || p[0] == 'I') &&
        (p[1] == 'n' || p[1] == 'N') &&
        (p[2] == 'f' || p[2] == 'F')) {
        p += 3;
        if ((p[0] == 'i' || p[0] == 'I') &&
            (p[1] == 'n' || p[1] == 'N') &&
            (p[2] == 'i' || p[2] == 'I') &&
            (p[3] == 't' || p[3] == 'T') &&
            (p[4] == 'y' || p[4] == 'Y')) p += 5;
        if (endptr) *endptr = (char *)p;
        return ((uint64_t)sign << 63) | 0x7FF0000000000000ULL;
    }
    if ((p[0] == 'n' || p[0] == 'N') &&
        (p[1] == 'a' || p[1] == 'A') &&
        (p[2] == 'n' || p[2] == 'N')) {
        p += 3;
        if (endptr) *endptr = (char *)p;
        return 0x7FF8000000000000ULL;
    }

    uint64_t mant    = 0;
    int      dec_exp = 0;
    int      seen_digit = 0;

    while (*p >= '0' && *p <= '9') {
        seen_digit = 1;
        if (mant <= (UINT64_MAX - 9) / 10) {
            mant = mant * 10 + (uint64_t)(*p - '0');
        } else {
            dec_exp++;
        }
        p++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            seen_digit = 1;
            if (mant <= (UINT64_MAX - 9) / 10) {
                mant = mant * 10 + (uint64_t)(*p - '0');
                dec_exp--;
            }
            p++;
        }
    }
    if (!seen_digit) {
        if (endptr) *endptr = (char *)s;
        return 0;
    }

    if (*p == 'e' || *p == 'E') {
        const char *ep = p + 1;
        int esign = 0;
        if (*ep == '+') ep++;
        else if (*ep == '-') { esign = 1; ep++; }
        if (*ep >= '0' && *ep <= '9') {
            int eval = 0;
            while (*ep >= '0' && *ep <= '9') {
                if (eval < 10000) eval = eval * 10 + (*ep - '0');
                ep++;
            }
            dec_exp += esign ? -eval : eval;
            p = ep;
        }
    }
    if (endptr) *endptr = (char *)p;

    if (mant == 0) {
        return (uint64_t)sign << 63;
    }

    int bin_exp = 0;
    while ((mant >> 63) == 0) {
        mant <<= 1;
        bin_exp--;
    }

    while (dec_exp > 0) {
        uint64_t a_hi = mant >> 32;
        uint64_t a_lo = mant & 0xFFFFFFFFULL;
        uint64_t p_lo = a_lo * 10;
        uint64_t p_hi = a_hi * 10;
        uint64_t mid_carry = p_lo >> 32;
        uint64_t lo = ((p_hi + mid_carry) << 32) | (p_lo & 0xFFFFFFFFULL);
        uint64_t hi = (p_hi + mid_carry) >> 32;

        while (hi != 0) {
            lo = (lo >> 1) | (hi << 63);
            hi >>= 1;
            bin_exp++;
        }
        mant = lo;
        while ((mant >> 63) == 0) {
            mant <<= 1;
            bin_exp--;
        }
        dec_exp--;
    }
    while (dec_exp < 0) {
        mant = mant / 10;
        if (mant == 0) break;
        while ((mant >> 63) == 0) {
            mant <<= 1;
            bin_exp--;
        }
        dec_exp++;
    }

    int ieee_exp = bin_exp + 63 + 1023;
    uint64_t no_implicit = mant & 0x7FFFFFFFFFFFFFFFULL;
    uint64_t round_bit   = (no_implicit >> 10) & 1;
    uint64_t sticky      = (no_implicit & 0x3FFULL) ? 1 : 0;
    uint64_t frac        = no_implicit >> 11;
    if (round_bit && (sticky || (frac & 1))) {
        frac++;
        if (frac == (1ULL << 52)) {
            frac = 0;
            ieee_exp++;
        }
    }

    if (ieee_exp >= 0x7FF) {
        return ((uint64_t)sign << 63) | 0x7FF0000000000000ULL;
    }
    if (ieee_exp <= 0) {
        return (uint64_t)sign << 63;
    }
    return ((uint64_t)sign << 63) |
           ((uint64_t)ieee_exp << 52) |
           (frac & 0xFFFFFFFFFFFFFULL);
}

double strtod(const char *s, char **endptr)
{
    uint64_t bits = __cervus_strtod_bits(s, endptr);
    double result;
    __builtin_memcpy(&result, &bits, sizeof(double));
    return result;
}

float strtof(const char *s, char **endptr)
{
    return (float)strtod(s, endptr);
}

long double strtold(const char *s, char **endptr)
{
    return (long double)strtod(s, endptr);
}

double atof(const char *s)
{
    return strtod(s, NULL);
}