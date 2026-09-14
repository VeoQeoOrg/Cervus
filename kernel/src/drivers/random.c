#include "../../include/drivers/random.h"
#include "../../include/io/serial.h"
#include "../../include/drivers/timer.h"
#include <string.h>

static uint32_t g_state[16];
static uint64_t g_counter;
static int      g_have_hw;
static int      g_ready;

static inline uint64_t rnd_rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void cpuid_leaf(uint32_t leaf, uint32_t sub, uint32_t out[4])
{
    __asm__ volatile ("cpuid"
                      : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
                      : "a"(leaf), "c"(sub));
}

static int hw_random64(uint64_t *out)
{
    if (!g_have_hw) return 0;
    for (int i = 0; i < 32; i++) {
        uint64_t v;
        unsigned char ok;
        __asm__ volatile ("rdrand %0; setc %1" : "=r"(v), "=qm"(ok));
        if (ok) { *out = v; return 1; }
    }
    return 0;
}

static int hw_seed64(uint64_t *out)
{
    if (!g_have_hw) return 0;
    for (int i = 0; i < 32; i++) {
        uint64_t v;
        unsigned char ok;
        __asm__ volatile ("rdseed %0; setc %1" : "=r"(v), "=qm"(ok));
        if (ok) { *out = v; return 1; }
    }
    return hw_random64(out);
}

static uint64_t jitter64(void)
{
    uint64_t acc = 0;
    for (int i = 0; i < 64; i++) {
        uint64_t a = rnd_rdtsc();
        for (volatile int k = 0; k < 7 + (int)(a & 15); k++) { }
        uint64_t b = rnd_rdtsc();
        acc = (acc << 1) | ((b - a) & 1);
        acc ^= (b << 13) ^ (a >> 7);
    }
    return acc;
}

#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void quarter(uint32_t *s, int a, int b, int c, int d)
{
    s[a] += s[b]; s[d] ^= s[a]; s[d] = ROTL32(s[d], 16);
    s[c] += s[d]; s[b] ^= s[c]; s[b] = ROTL32(s[b], 12);
    s[a] += s[b]; s[d] ^= s[a]; s[d] = ROTL32(s[d], 8);
    s[c] += s[d]; s[b] ^= s[c]; s[b] = ROTL32(s[b], 7);
}

static void chacha_block(const uint32_t in[16], uint32_t out[16])
{
    for (int i = 0; i < 16; i++) out[i] = in[i];
    for (int i = 0; i < 10; i++) {
        quarter(out, 0, 4,  8, 12);
        quarter(out, 1, 5,  9, 13);
        quarter(out, 2, 6, 10, 14);
        quarter(out, 3, 7, 11, 15);
        quarter(out, 0, 5, 10, 15);
        quarter(out, 1, 6, 11, 12);
        quarter(out, 2, 7,  8, 13);
        quarter(out, 3, 4,  9, 14);
    }
    for (int i = 0; i < 16; i++) out[i] += in[i];
}

void random_add_entropy(const void *buf, size_t len)
{
    const uint8_t *p = buf;
    for (size_t i = 0; i < len; i++)
        g_state[4 + (i % 8)] ^= (uint32_t)p[i] << ((i % 4) * 8);
    g_state[12] ^= (uint32_t)rnd_rdtsc();
}

void random_init(void)
{
    uint32_t r[4];
    cpuid_leaf(0, 0, r);
    uint32_t maxleaf = r[0];
    g_have_hw = 0;
    if (maxleaf >= 1) {
        cpuid_leaf(1, 0, r);
        if (r[2] & (1u << 30)) g_have_hw = 1;
    }

    g_state[0] = 0x61707865; g_state[1] = 0x3320646e;
    g_state[2] = 0x79622d32; g_state[3] = 0x6b206574;

    for (int i = 0; i < 8; i += 2) {
        uint64_t v = 0;
        if (!hw_seed64(&v)) v = jitter64() ^ rnd_rdtsc();
        g_state[4 + i]     = (uint32_t)v;
        g_state[4 + i + 1] = (uint32_t)(v >> 32);
    }
    g_state[12] = (uint32_t)rnd_rdtsc();
    g_state[13] = (uint32_t)(rnd_rdtsc() >> 32);
    g_state[14] = (uint32_t)jitter64();
    g_state[15] = (uint32_t)(jitter64() >> 32);

    g_counter = 0;
    g_ready = 1;
    serial_printf("[random] seeded from %s\n",
                  g_have_hw ? "RDSEED/RDRAND" : "timing jitter");
}

void random_bytes(void *buf, size_t len)
{
    if (!g_ready) random_init();

    uint8_t *out = buf;
    uint32_t block[16];

    while (len) {
        g_state[12] = (uint32_t)(++g_counter);
        g_state[13] = (uint32_t)(g_counter >> 32);
        g_state[14] ^= (uint32_t)rnd_rdtsc();

        uint64_t hw = 0;
        if (hw_random64(&hw)) {
            g_state[10] ^= (uint32_t)hw;
            g_state[11] ^= (uint32_t)(hw >> 32);
        }

        chacha_block(g_state, block);

        size_t take = len < sizeof(block) ? len : sizeof(block);
        memcpy(out, block, take);
        out += take;
        len -= take;

        g_state[4] ^= block[0];
        g_state[5] ^= block[1];
    }
    memset(block, 0, sizeof block);
}
