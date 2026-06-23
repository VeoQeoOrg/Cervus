#include <string.h>

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || n == 0) return dest;

    size_t q = n >> 3;
    size_t b = n & 7;

    if (d < s) {
        asm volatile (
            "cld\n\t"
            "rep movsq"
            : "+D"(d), "+S"(s), "+c"(q)
            :: "memory", "cc"
        );
        asm volatile (
            "rep movsb"
            : "+D"(d), "+S"(s), "+c"(b)
            :: "memory", "cc"
        );
    } else {
        uint8_t *db = d + n - 1;
        const uint8_t *sb = s + n - 1;
        asm volatile (
            "std\n\t"
            "rep movsb"
            : "+D"(db), "+S"(sb), "+c"(b)
            :: "memory", "cc"
        );
        uint64_t *dq = (uint64_t *)(db - 7);
        const uint64_t *sq = (const uint64_t *)(sb - 7);
        asm volatile (
            "rep movsq\n\t"
            "cld"
            : "+D"(dq), "+S"(sq), "+c"(q)
            :: "memory", "cc"
        );
    }
    return dest;
}
