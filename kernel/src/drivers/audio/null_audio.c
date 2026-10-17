#include "../../../include/drivers/audio/audio.h"
#include "../../../include/io/serial.h"
#include <stdlib.h>
#include <string.h>

#define NULL_RING 65536

extern uint64_t sched_now_ns(void);

static struct {
    uint8_t *ring;
    uint32_t rate;
    uint32_t used;
    uint64_t start_ns;
    uint32_t base;
    int      running;
    int      volume;
    int      mute;
} N;

static uint32_t null_fix_rate(uint32_t rate) {
    if (rate < PCM_MIN_RATE) rate = PCM_MIN_RATE;
    if (rate > PCM_MAX_RATE) rate = PCM_MAX_RATE;
    return rate;
}

static int null_prepare(uint32_t rate, uint32_t *period, uint32_t *periods) {
    uint32_t pb = (*period + 3) & ~3u;
    uint32_t n = *periods;
    if (n * pb > NULL_RING) n = NULL_RING / pb;
    if (n < 2) { n = 2; pb = NULL_RING / 2; }
    N.rate = rate;
    N.used = n * pb;
    N.running = 0;
    N.base = 0;
    *period = pb;
    *periods = n;
    return 0;
}

static uint32_t null_pointer(void) {
    if (!N.used) return 0;
    if (!N.running) return N.base;
    uint64_t ns = sched_now_ns() - N.start_ns;
    uint64_t frames = ns * N.rate / 1000000000ULL;
    return (uint32_t)((N.base + frames * 4) % N.used);
}

static void null_trigger(int run) {
    if (run && !N.running) {
        N.start_ns = sched_now_ns();
        N.running = 1;
    } else if (!run && N.running) {
        N.base = null_pointer();
        N.running = 0;
    }
}

static int null_mixer_get(audio_mixer_t *m) {
    memset(m, 0, sizeof *m);
    strncpy(m->driver, "null", sizeof m->driver - 1);
    m->volume = N.volume;
    m->mute = N.mute;
    m->noutputs = 1;
    m->current = 0;
    strncpy(m->outputs[0].name, "Silent", sizeof m->outputs[0].name - 1);
    m->outputs[0].kind = AUDIO_OUT_OTHER;
    m->outputs[0].present = 1;
    return 0;
}

static int null_set_volume(int pct) { N.volume = pct; return 0; }
static int null_set_mute(int mute) { N.mute = mute; return 0; }
static int null_set_output(int idx) { return idx <= 0 ? 0 : -1; }

static audio_backend_t g_null_backend = {
    .name        = "null",
    .ring_max    = NULL_RING,
    .max_periods = 256,
    .fix_rate    = null_fix_rate,
    .prepare     = null_prepare,
    .trigger     = null_trigger,
    .pointer     = null_pointer,
    .mixer_get   = null_mixer_get,
    .set_volume  = null_set_volume,
    .set_mute    = null_set_mute,
    .set_output  = null_set_output,
};

void audio_null_init(void) {
    if (audio_present()) return;
    N.ring = calloc(1, NULL_RING);
    if (!N.ring) return;
    N.volume = 75;
    g_null_backend.ring = N.ring;
    audio_register(&g_null_backend);
    serial_printf("[audio] no sound hardware, using the silent device\n");
}
