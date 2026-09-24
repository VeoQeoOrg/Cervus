#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define DEG 31
#define SEP 3

static int32_t g_tbl[DEG];
static int g_front = SEP, g_rear = 0;
static int g_seeded;

static void seed_table(int32_t *t, unsigned int seed)
{
    t[0] = (int32_t)(seed ? seed : 1);
    for (int i = 1; i < DEG; i++) {
        int64_t v = (16807LL * t[i - 1]) % 2147483647;
        if (v < 0) v += 2147483647;
        t[i] = (int32_t)v;
    }
}

static long next_value(void)
{
    uint32_t v = (uint32_t)g_tbl[g_front] + (uint32_t)g_tbl[g_rear];
    g_tbl[g_front] = (int32_t)v;
    if (++g_front >= DEG) g_front = 0;
    if (++g_rear >= DEG) g_rear = 0;
    return (long)(v >> 1);
}

void srandom(unsigned int seed)
{
    seed_table(g_tbl, seed);
    g_front = SEP;
    g_rear = 0;
    g_seeded = 1;
    for (int i = 0; i < 10 * DEG; i++) next_value();
}

long random(void)
{
    if (!g_seeded) srandom(1);
    return next_value();
}

static char g_state_copy[DEG * sizeof(int32_t)];

char *initstate(unsigned int seed, char *state, size_t n)
{
    (void)state;
    (void)n;
    memcpy(g_state_copy, g_tbl, sizeof g_state_copy);
    srandom(seed);
    return g_state_copy;
}

char *setstate(char *state)
{
    static char prev[DEG * sizeof(int32_t)];
    memcpy(prev, g_tbl, sizeof prev);
    if (state) memcpy(g_tbl, state, sizeof g_tbl);
    g_seeded = 1;
    return prev;
}
