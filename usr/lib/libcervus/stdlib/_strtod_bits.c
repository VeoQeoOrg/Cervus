#include <stdint.h>
#include <stddef.h>

#define SD_WORDS 208

typedef struct { uint32_t w[SD_WORDS]; int n; } sd_bn;

static void bn_set64(sd_bn *b, uint64_t v)
{
    for (int i = 0; i < SD_WORDS; i++) b->w[i] = 0;
    b->w[0] = (uint32_t)(v & 0xFFFFFFFFULL);
    b->w[1] = (uint32_t)(v >> 32);
    b->n = b->w[1] ? 2 : 1;
}

static uint64_t __sd_assemble_exp(sd_bn *num, int bin_exp, int sticky, int sign);

static uint64_t __sd_assemble(sd_bn *num, int bin_exp, int sticky, int sign)
{
    return __sd_assemble_exp(num, bin_exp, sticky, sign);
}

static void bn_add_small(sd_bn *b, uint32_t k)
{
    uint64_t carry = k;
    for (int i = 0; i < b->n && carry; i++) {
        uint64_t cur = (uint64_t)b->w[i] + carry;
        b->w[i] = (uint32_t)cur;
        carry = cur >> 32;
    }
    while (carry && b->n < SD_WORDS) {
        b->w[b->n++] = (uint32_t)carry;
        carry >>= 32;
    }
}

static void bn_mul_small(sd_bn *b, uint32_t k)
{
    uint64_t carry = 0;
    for (int i = 0; i < b->n; i++) {
        uint64_t cur = (uint64_t)b->w[i] * k + carry;
        b->w[i] = (uint32_t)cur;
        carry = cur >> 32;
    }
    while (carry && b->n < SD_WORDS) {
        b->w[b->n++] = (uint32_t)carry;
        carry >>= 32;
    }
}

static uint32_t bn_div_small(sd_bn *b, uint32_t k)
{
    uint64_t rem = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        uint64_t cur = (rem << 32) | b->w[i];
        b->w[i] = (uint32_t)(cur / k);
        rem = cur % k;
    }
    while (b->n > 1 && b->w[b->n - 1] == 0) b->n--;
    return (uint32_t)rem;
}

static void bn_shl(sd_bn *b, int bits)
{
    int words = bits / 32;
    int rest  = bits % 32;

    if (words) {
        int limit = b->n + words;
        if (limit > SD_WORDS) limit = SD_WORDS;
        for (int i = limit - 1; i >= words; i--) b->w[i] = b->w[i - words];
        for (int i = 0; i < words && i < SD_WORDS; i++) b->w[i] = 0;
        b->n = limit;
    }
    if (rest) {
        uint32_t carry = 0;
        for (int i = 0; i < b->n; i++) {
            uint64_t cur = ((uint64_t)b->w[i] << rest) | carry;
            b->w[i] = (uint32_t)cur;
            carry = (uint32_t)(cur >> 32);
        }
        if (carry && b->n < SD_WORDS) b->w[b->n++] = carry;
    }
    while (b->n > 1 && b->w[b->n - 1] == 0) b->n--;
}

static int bn_bitlen(const sd_bn *b)
{
    for (int i = b->n - 1; i >= 0; i--) {
        if (!b->w[i]) continue;
        int bit = 31;
        while (bit >= 0 && !((b->w[i] >> bit) & 1u)) bit--;
        return i * 32 + bit + 1;
    }
    return 0;
}

static int bn_low_nonzero(const sd_bn *b, int bits)
{
    int words = bits / 32;
    int rest  = bits % 32;
    for (int i = 0; i < words && i < b->n; i++)
        if (b->w[i]) return 1;
    if (rest && words < b->n && (b->w[words] & ((1u << rest) - 1u))) return 1;
    return 0;
}

static uint64_t bn_extract64(const sd_bn *b, int from)
{
    int words = from / 32;
    int rest  = from % 32;
    uint64_t out = 0;
    for (int i = 0; i < 3; i++) {
        int idx = words + i;
        uint64_t part = (idx < b->n) ? b->w[idx] : 0;
        out |= part << (32 * i);
        if (i == 1) break;
    }
    uint64_t hi = (words + 2 < b->n) ? b->w[words + 2] : 0;
    if (rest) out = (out >> rest) | (hi << (64 - rest));
    return out;
}

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

    sd_bn num;
    bn_set64(&num, 0);

    int dec_exp    = 0;
    int seen_digit = 0;
    int ndig       = 0;
    int sticky     = 0;
    int any        = 0;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        const char *hp = p + 2;
        int hex_digits = 0;
        int hex_exp = 0;
        sd_bn hx;
        bn_set64(&hx, 0);
        while ((*hp >= '0' && *hp <= '9') ||
               (*hp >= 'a' && *hp <= 'f') || (*hp >= 'A' && *hp <= 'F')) {
            unsigned d = (*hp <= '9') ? (unsigned)(*hp - '0')
                                      : (unsigned)((*hp | 32) - 'a' + 10);
            if (bn_bitlen(&hx) < 5000) { bn_shl(&hx, 4); bn_add_small(&hx, d); }
            else { hex_exp += 4; if (d) sticky = 1; }
            hex_digits++;
            hp++;
        }
        if (*hp == '.') {
            hp++;
            while ((*hp >= '0' && *hp <= '9') ||
                   (*hp >= 'a' && *hp <= 'f') || (*hp >= 'A' && *hp <= 'F')) {
                unsigned d = (*hp <= '9') ? (unsigned)(*hp - '0')
                                          : (unsigned)((*hp | 32) - 'a' + 10);
                if (bn_bitlen(&hx) < 5000) { bn_shl(&hx, 4); bn_add_small(&hx, d); hex_exp -= 4; }
                else if (d) sticky = 1;
                hex_digits++;
                hp++;
            }
        }
        if (hex_digits) {
            if (*hp == 'p' || *hp == 'P') {
                const char *ep = hp + 1;
                int esign = 0;
                if (*ep == '+') ep++;
                else if (*ep == '-') { esign = 1; ep++; }
                if (*ep >= '0' && *ep <= '9') {
                    int ev = 0;
                    while (*ep >= '0' && *ep <= '9') {
                        if (ev < 100000) ev = ev * 10 + (*ep - '0');
                        ep++;
                    }
                    hex_exp += esign ? -ev : ev;
                    hp = ep;
                }
            }
            if (endptr) *endptr = (char *)hp;
            return __sd_assemble(&hx, hex_exp, sticky, sign);
        }
    }

    while (*p >= '0' && *p <= '9') {
        seen_digit = 1;
        if (*p != '0' || any) {
            if (ndig < 800) {
                bn_mul_small(&num, 10);
                bn_add_small(&num, (uint32_t)(*p - '0'));
                ndig++;
                any = 1;
            } else {
                if (*p != '0') sticky = 1;
                dec_exp++;
            }
        }
        p++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            seen_digit = 1;
            if (*p != '0' || any) {
                if (ndig < 800) {
                    bn_mul_small(&num, 10);
                    bn_add_small(&num, (uint32_t)(*p - '0'));
                    ndig++;
                    dec_exp--;
                    any = 1;
                } else if (*p != '0') {
                    sticky = 1;
                }
            } else {
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
                if (eval < 100000) eval = eval * 10 + (*ep - '0');
                ep++;
            }
            dec_exp += esign ? -eval : eval;
            p = ep;
        }
    }
    if (endptr) *endptr = (char *)p;

    if (!any) return (uint64_t)sign << 63;

    if (dec_exp + ndig > 320)  return ((uint64_t)sign << 63) | 0x7FF0000000000000ULL;
    if (dec_exp + ndig < -350) return (uint64_t)sign << 63;

    int bin_exp = 0;
    if (dec_exp > 0) {
        int k = dec_exp;
        while (k >= 13) { bn_mul_small(&num, 1220703125u); k -= 13; }
        while (k-- > 0)   bn_mul_small(&num, 5u);
        bin_exp += dec_exp;
    } else if (dec_exp < 0) {
        int q = -dec_exp;
        int shift = (q * 2322) / 1000 + 68;
        bn_shl(&num, shift);
        bin_exp -= shift + q;
        int k = q;
        while (k >= 13) { if (bn_div_small(&num, 1220703125u)) sticky = 1; k -= 13; }
        while (k-- > 0)   { if (bn_div_small(&num, 5u)) sticky = 1; }
    }

    return __sd_assemble_exp(&num, bin_exp, sticky, sign);
}

static uint64_t __sd_assemble_exp(sd_bn *num, int bin_exp, int sticky, int sign)
{
    int len = bn_bitlen(num);
    if (len == 0) return (uint64_t)sign << 63;

    uint64_t q64;
    if (len > 64) {
        int drop = len - 64;
        if (bn_low_nonzero(num, drop)) sticky = 1;
        q64 = bn_extract64(num, drop);
        bin_exp += drop;
    } else {
        q64 = bn_extract64(num, 0) << (64 - len);
        bin_exp -= 64 - len;
    }

    int ieee_exp = bin_exp + 63 + 1023;

    int shift = 11;
    if (ieee_exp <= 0) {
        shift += 1 - ieee_exp;
        ieee_exp = 0;
        if (shift > 64) return (uint64_t)sign << 63;
    }

    uint64_t frac      = (shift >= 64) ? 0 : (q64 >> shift);
    uint64_t round_bit = (shift == 0) ? 0 : ((q64 >> (shift - 1)) & 1ULL);
    uint64_t rest      = (shift <= 1) ? 0 : (q64 & ((1ULL << (shift - 1)) - 1ULL));
    if (rest) sticky = 1;

    if (round_bit && (sticky || (frac & 1ULL))) {
        frac++;
        if (ieee_exp == 0) {
            if (frac == (1ULL << 52)) { ieee_exp = 1; frac = 0; }
        } else if (frac == (1ULL << 53)) {
            frac >>= 1;
            ieee_exp++;
        }
    }

    if (ieee_exp >= 0x7FF)
        return ((uint64_t)sign << 63) | 0x7FF0000000000000ULL;

    return ((uint64_t)sign << 63) |
           ((uint64_t)ieee_exp << 52) |
           (frac & 0xFFFFFFFFFFFFFULL);
}
