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
#define PCM_MAX_RATE     192000
#define PCM_MAX_CHANNELS 8
#define PCM_MAX_STREAMS  8

typedef struct {
    uint32_t bytes;
    uint32_t fragsize;
    uint32_t fragments;
    uint32_t fragstotal;
} pcm_space_t;

void audio_register(const audio_backend_t *b);
int  audio_present(void);
const char *audio_backend_name(void);

typedef struct pcm_stream pcm_stream_t;

pcm_stream_t *pcm_open(void *owner);
pcm_stream_t *pcm_find(void *owner);
void     pcm_close(pcm_stream_t *s, int drain);
int      pcm_streams_open(void);
uint32_t pcm_device_rate(void);
uint32_t pcm_set_rate(pcm_stream_t *s, uint32_t rate);
int      pcm_set_channels(pcm_stream_t *s, int ch);
int      pcm_set_format(pcm_stream_t *s, int fmt);
uint32_t pcm_set_fragment(pcm_stream_t *s, uint32_t frag_bytes, uint32_t nfrags);
long     pcm_write(pcm_stream_t *s, const void *buf, size_t len, int nonblock);
void     pcm_space(pcm_stream_t *s, pcm_space_t *sp);
uint32_t pcm_delay(pcm_stream_t *s);
void     pcm_position(pcm_stream_t *s, uint64_t *played, uint32_t *ptr, uint32_t *frags_done);
void     pcm_reset(pcm_stream_t *s);
int      pcm_sync(pcm_stream_t *s);
void     pcm_post(pcm_stream_t *s);
void     pcm_set_trigger(pcm_stream_t *s, int enable);
int      pcm_get_trigger(pcm_stream_t *s);
int      pcm_get_volume(pcm_stream_t *s);
void     pcm_set_volume(pcm_stream_t *s, int lr);
int      pcm_get_format(pcm_stream_t *s);
int      pcm_get_channels(pcm_stream_t *s);
uint32_t pcm_get_rate(pcm_stream_t *s);
uint32_t pcm_xruns(pcm_stream_t *s);
int      pcm_writable(pcm_stream_t *s);
void     pcm_poll_wait(pcm_stream_t *s);
int      pcm_status(char *buf, size_t max);

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
