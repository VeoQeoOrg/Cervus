#include "../../../include/drivers/audio/audio.h"
#include "../../../include/io/serial.h"

static const audio_backend_t *g_backend;

void audio_register(const audio_backend_t *b) {
    if (!b) return;
    if (g_backend) return;
    g_backend = b;
    serial_printf("[audio] backend: %s\n", b->name);
}

int audio_present(void) { return g_backend != 0; }

int audio_open(uint32_t rate) {
    if (!g_backend) return -1;
    return g_backend->open(rate);
}

long audio_write(const void *pcm, size_t bytes) {
    if (!g_backend) return -1;
    return g_backend->write(pcm, bytes);
}

int audio_close(void) {
    if (!g_backend) return -1;
    return g_backend->close();
}

int audio_mixer_get(audio_mixer_t *m) {
    if (!g_backend || !g_backend->mixer_get || !m) return -1;
    return g_backend->mixer_get(m);
}

int audio_set_volume(int pct) {
    if (!g_backend || !g_backend->set_volume) return -1;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return g_backend->set_volume(pct);
}

int audio_set_mute(int mute) {
    if (!g_backend || !g_backend->set_mute) return -1;
    return g_backend->set_mute(mute ? 1 : 0);
}

int audio_set_output(int idx) {
    if (!g_backend || !g_backend->set_output) return -1;
    return g_backend->set_output(idx);
}
