#ifndef KERNEL_DRIVERS_AUDIO_HDA_CODEC_H
#define KERNEL_DRIVERS_AUDIO_HDA_CODEC_H

#include <stdint.h>

#define HDA_MAX_PATH 8

typedef uint32_t (*hda_cmd_fn)(int nid, uint32_t verb);

typedef struct {
    int nid;
    int dac;
    int kind;
    int has_presence;
    int has_eapd;
    int nchain;
    int chain[HDA_MAX_PATH];
    int sel[HDA_MAX_PATH];
    int amp_nid;
    int amp_steps;
    int amp_mute_cap;
} hda_path_t;

int  hda_codec_scan(hda_cmd_fn cmd, hda_path_t *out, int max, int *afg_out);
int  hda_codec_present(hda_cmd_fn cmd, const hda_path_t *p);
void hda_codec_amp(hda_cmd_fn cmd, const hda_path_t *p, int volume, int mute, int enable);
void hda_codec_enable(hda_cmd_fn cmd, const hda_path_t *p, uint16_t fmt, int stream_tag);
void hda_codec_disable(hda_cmd_fn cmd, const hda_path_t *p);
const char *hda_kind_name(int kind);

#endif
