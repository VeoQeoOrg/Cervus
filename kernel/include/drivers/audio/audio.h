#ifndef KERNEL_DRIVERS_AUDIO_AUDIO_H
#define KERNEL_DRIVERS_AUDIO_AUDIO_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char *name;
    int  (*open)(uint32_t rate);
    long (*write)(const void *pcm, size_t bytes);
    int  (*close)(void);
} audio_backend_t;

void audio_register(const audio_backend_t *b);
int  audio_present(void);

int  audio_open(uint32_t rate);
long audio_write(const void *pcm, size_t bytes);
int  audio_close(void);

#endif
