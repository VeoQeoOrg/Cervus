#ifndef KERNEL_DRIVERS_AUDIO_AUDIO_H
#define KERNEL_DRIVERS_AUDIO_AUDIO_H

#include <stdint.h>
#include <stddef.h>

#define AUDIO_MAX_OUTPUTS 8

typedef struct {
    char     name[24];
    uint8_t  kind;
    uint8_t  present;
    uint8_t  _pad[2];
} audio_output_t;

typedef struct {
    char           driver[16];
    int32_t        volume;
    int32_t        mute;
    int32_t        noutputs;
    int32_t        current;
    audio_output_t outputs[AUDIO_MAX_OUTPUTS];
} audio_mixer_t;

#define AUDIO_OUT_SPEAKER  0
#define AUDIO_OUT_HEADPHONE 1
#define AUDIO_OUT_LINEOUT  2
#define AUDIO_OUT_DIGITAL  3
#define AUDIO_OUT_OTHER    4

typedef struct {
    const char *name;
    int  (*open)(uint32_t rate);
    long (*write)(const void *pcm, size_t bytes);
    int  (*close)(void);
    int  (*mixer_get)(audio_mixer_t *m);
    int  (*set_volume)(int pct);
    int  (*set_mute)(int mute);
    int  (*set_output)(int idx);
} audio_backend_t;

void audio_register(const audio_backend_t *b);
int  audio_present(void);

int  audio_open(uint32_t rate);
long audio_write(const void *pcm, size_t bytes);
int  audio_close(void);

int  audio_mixer_get(audio_mixer_t *m);
int  audio_set_volume(int pct);
int  audio_set_mute(int mute);
int  audio_set_output(int idx);

#endif
