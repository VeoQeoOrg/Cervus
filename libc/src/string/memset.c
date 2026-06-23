#include <string.h>

void *memset(void *s, int c, size_t n) {
    void *ret = s;
    uint8_t cb = (uint8_t)c;
    uint64_t c8 = 0x0101010101010101ULL * (uint64_t)cb;
    size_t q = n >> 3;
    size_t b = n & 7;
    asm volatile (
        "cld\n\t"
        "rep stosq"
        : "+D"(s), "+c"(q)
        : "a"(c8)
        : "memory", "cc"
    );
    asm volatile (
        "rep stosb"
        : "+D"(s), "+c"(b)
        : "a"(cb)
        : "memory", "cc"
    );
    return ret;
}
