#include <crypto.h>
#include <sys/cervus.h>
#include <string.h>

static int has_rdrand(void) {
    uint32_t a = 1, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return (c >> 30) & 1;
}

static int rdrand64(uint64_t *v) {
    unsigned char ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(*v), "=qm"(ok));
    return ok;
}

static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void crypto_random(void *buf, size_t len) {
    static uint64_t ctr = 0;
    uint8_t seed[64];
    sha512_ctx sc;
    sha512_init(&sc);

    int rd = has_rdrand();
    for (int i = 0; i < 24; i++) {
        uint64_t v = 0;
        if (rd) { int t = 0; while (!rdrand64(&v) && t++ < 16) {} }
        v ^= rdtsc();
        v ^= cervus_uptime_ns();
        sha512_update(&sc, &v, sizeof v);
    }
    uintptr_t sp = (uintptr_t)&sc;
    sha512_update(&sc, &sp, sizeof sp);
    ctr++;
    sha512_update(&sc, &ctr, sizeof ctr);
    sha512_final(&sc, seed);

    uint8_t key[32], nonce[12];
    memcpy(key, seed, 32);
    memcpy(nonce, seed + 32, 12);

    uint8_t zeros[256];
    memset(zeros, 0, sizeof zeros);
    size_t off = 0;
    uint32_t counter = 0;
    while (off < len) {
        size_t n = len - off;
        if (n > sizeof zeros) n = sizeof zeros;
        chacha20(key, nonce, counter, zeros, (uint8_t *)buf + off, n);
        counter += (uint32_t)((n + 63) / 64);
        off += n;
    }
}
