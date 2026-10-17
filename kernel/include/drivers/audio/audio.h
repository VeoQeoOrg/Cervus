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
    uint8_t    *ring;
    uint32_t    ring_max;
    uint32_t    max_periods;
    uint32_t  (*fix_rate)(uint32_t rate);
    int       (*prepare)(uint32_t rate, uint32_t *period_bytes, uint32_t *periods);
    void      (*trigger)(int run);
    uint32_t  (*pointer)(void);
    void      (*tick)(void);
    int       (*mixer_get)(audio_mixer_t *m);
    int       (*set_volume)(int pct);
    int       (*set_mute)(int mute);
    int       (*set_output)(int idx);
} audio_backend_t;

#define PCM_FMT_U8     0x00000008
#define PCM_FMT_S16_LE 0x00000010
#define PCM_FMT_S32_LE 0x00001000
#define PCM_FMT_FLOAT  0x00004000
#define PCM_FMT_S24_LE 0x00008000
#define PCM_FMTS (PCM_FMT_U8 | PCM_FMT_S16_LE | PCM_FMT_S32_LE | PCM_FMT_FLOAT | PCM_FMT_S24_LE)

#define PCM_MIN_RATE     8000
#define PCM_MAX_RATE     96000
#define PCM_MAX_CHANNELS 8

typedef struct {
    uint32_t bytes;
    uint32_t fragsize;
    uint32_t fragments;
    uint32_t fragstotal;
} pcm_space_t;

void audio_register(const audio_backend_t *b);
int  audio_present(void);
const char *audio_backend_name(void);

int      pcm_claim(void *owner);
void     pcm_release(void *owner, int drain);
void    *pcm_owner(void);
uint32_t pcm_set_rate(uint32_t rate);
int      pcm_set_channels(int ch);
int      pcm_set_format(int fmt);
uint32_t pcm_set_fragment(uint32_t frag_bytes, uint32_t nfrags);
long     pcm_write(const void *buf, size_t len, int nonblock);
void     pcm_space(pcm_space_t *s);
uint32_t pcm_delay(void);
void     pcm_position(uint64_t *played, uint32_t *ptr, uint32_t *frags_done);
void     pcm_reset(void);
int      pcm_sync(void);
void     pcm_post(void);
void     pcm_set_trigger(int enable);
int      pcm_get_trigger(void);
int      pcm_get_volume(void);
void     pcm_set_volume(int lr);
int      pcm_get_format(void);
int      pcm_get_channels(void);
uint32_t pcm_get_rate(void);
uint32_t pcm_xruns(void);
int      pcm_writable(void);

int  audio_open(uint32_t rate);
long audio_write(const void *pcm, size_t bytes);
int  audio_close(void);

void audio_set_owner(void *task);
void audio_task_exit(void *task);

int  audio_mixer_get(audio_mixer_t *m);
int  audio_set_volume(int pct);
int  audio_set_mute(int mute);
int  audio_set_output(int idx);

void oss_init(void);
void audio_null_init(void);

#endif
