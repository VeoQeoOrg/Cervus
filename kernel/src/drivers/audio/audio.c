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
