#include <string.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    void *ret = dest;
    size_t q = n >> 3;
    size_t b = n & 7;
    asm volatile (
        "cld\n\t"
        "rep movsq"
        : "+D"(dest), "+S"(src), "+c"(q)
        :: "memory", "cc"
    );
    asm volatile (
        "rep movsb"
        : "+D"(dest), "+S"(src), "+c"(b)
        :: "memory", "cc"
    );
    return ret;
}
