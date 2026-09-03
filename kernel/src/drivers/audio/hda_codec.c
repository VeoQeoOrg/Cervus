#include "../../../include/drivers/audio/hda_codec.h"
#include "../../../include/drivers/audio/audio.h"
#include "../../../include/io/serial.h"
#include <string.h>

#define VERB12(v, p)  (((uint32_t)(v) << 8) | ((uint32_t)(p) & 0xFF))
#define VERB4(v, p)   (((uint32_t)(v) << 16) | ((uint32_t)(p) & 0xFFFF))

#define V_GET_PARAM     0xF00
#define V_GET_CONNLIST  0xF02
#define V_GET_PINCTL    0xF07
#define V_GET_PINSENSE  0xF09
#define V_GET_CFGDEF    0xF1C
#define V_SET_POWER     0x705
#define V_SET_CONV      0x706
#define V_SET_PINCTL    0x707
#define V_SET_CONNSEL   0x701
#define V_SET_EAPD      0x70C
#define V_SET_FORMAT    0x2
#define V_SET_AMP       0x3

#define PARAM_NODE_COUNT    0x04
#define PARAM_FN_TYPE       0x05
#define PARAM_WIDGET_CAP    0x09
#define PARAM_PIN_CAP       0x0C
#define PARAM_CONN_LEN      0x0E
#define PARAM_OUT_AMP_CAP   0x12

#define WIDGET_DAC          0x0
#define WIDGET_ADC          0x1
#define WIDGET_SELECTOR     0x3
#define WIDGET_PIN          0x4

#define WCAP_OUT_AMP        (1u << 2)
#define WCAP_CONN_LIST      (1u << 8)
#define WCAP_DIGITAL        (1u << 9)
#define WCAP_POWER          (1u << 10)

#define PINCAP_PRESENCE     (1u << 2)
#define PINCAP_HP_DRIVE     (1u << 3)
#define PINCAP_OUTPUT       (1u << 4)
#define PINCAP_EAPD         (1u << 16)

#define PINCTL_HP_EN        (1u << 7)
#define PINCTL_OUT_EN       (1u << 6)

#define CFG_CONN_NONE       1
#define CFG_CONN_FIXED      2
#define DEV_LINE_OUT        0
#define DEV_SPEAKER         1
#define DEV_HP_OUT          2

#define AMP_SET_OUT         (1u << 15)
#define AMP_SET_LEFT        (1u << 13)
#define AMP_SET_RIGHT       (1u << 12)
#define AMP_MUTE            (1u << 7)

#define HDA_MAX_WIDGETS     128

static uint32_t param(hda_cmd_fn cmd, int nid, uint8_t p) {
    return cmd(nid, VERB12(V_GET_PARAM, p));
}

#define HDA_MAX_CONN 32

static int conn_list(hda_cmd_fn cmd, int nid, int *out, int max) {
    if (!(param(cmd, nid, PARAM_WIDGET_CAP) & WCAP_CONN_LIST)) return 0;

    uint32_t lenp = param(cmd, nid, PARAM_CONN_LEN);
    int nentries  = (int)(lenp & 0x7F);
    int longform  = (lenp & 0x80) != 0;
    int per       = longform ? 2 : 4;
    uint32_t rangebit = longform ? 0x8000u : 0x80u;
    uint32_t mask     = longform ? 0x7FFFu : 0x7Fu;

    int n = 0, prev = -1;
    for (int i = 0; i < nentries && n < max; i++) {
        uint32_t resp = cmd(nid, VERB12(V_GET_CONNLIST, (i / per) * per));
        int slot = i % per;
        uint32_t e = longform ? ((resp >> (slot * 16)) & 0xFFFF)
                             : ((resp >> (slot * 8)) & 0xFF);
        int val = (int)(e & mask);
        if ((e & rangebit) && prev >= 0) {
            for (int v = prev + 1; v <= val && n < max; v++) out[n++] = v;
        } else {
            out[n++] = val;
        }
        prev = val;
    }
    return n;
}

static int find_dac(hda_cmd_fn cmd, int nid, int depth, hda_path_t *p) {
    if (depth >= HDA_MAX_PATH) return -1;
    uint32_t cap = param(cmd, nid, PARAM_WIDGET_CAP);
    int type = (cap >> 20) & 0xF;

    if (type == WIDGET_DAC) {
        p->chain[depth] = nid;
        p->sel[depth] = -1;
        p->nchain = depth + 1;
        p->dac = nid;
        return 0;
    }
    if (depth > 0 && (type == WIDGET_ADC || type == WIDGET_PIN)) return -1;

    int conns[HDA_MAX_CONN];
    int n = conn_list(cmd, nid, conns, HDA_MAX_CONN);
    for (int i = 0; i < n; i++) {
        int child = conns[i];
        if (child <= 0) continue;
        int loop = 0;
        for (int d = 0; d < depth; d++) if (p->chain[d] == child) loop = 1;
        if (loop) continue;
        p->chain[depth] = nid;
        p->sel[depth] = i;
        if (find_dac(cmd, child, depth + 1, p) == 0) return 0;
    }
    return -1;
}

static void pick_amp(hda_cmd_fn cmd, hda_path_t *p) {
    p->amp_nid = -1;
    p->amp_steps = 0;
    p->amp_mute_cap = 0;
    for (int i = p->nchain - 1; i >= 0; i--) {
        int nid = p->chain[i];
        if (!(param(cmd, nid, PARAM_WIDGET_CAP) & WCAP_OUT_AMP)) continue;
        uint32_t amp = param(cmd, nid, PARAM_OUT_AMP_CAP);
        int steps = (int)((amp >> 8) & 0x7F);
        if (steps == 0) continue;
        p->amp_nid = nid;
        p->amp_steps = steps;
        p->amp_mute_cap = (amp & 0x80000000u) != 0;
        return;
    }
}

const char *hda_kind_name(int kind) {
    switch (kind) {
        case AUDIO_OUT_SPEAKER:   return "Speaker";
        case AUDIO_OUT_HEADPHONE: return "Headphone";
        case AUDIO_OUT_LINEOUT:   return "Line Out";
        case AUDIO_OUT_DIGITAL:   return "Digital Out";
        default:                  return "Output";
    }
}

int hda_codec_present(hda_cmd_fn cmd, const hda_path_t *p) {
    if (p->kind == AUDIO_OUT_SPEAKER) return 1;
    if (!p->has_presence) return 1;
    return (cmd(p->nid, VERB12(V_GET_PINSENSE, 0)) & 0x80000000u) ? 1 : 0;
}

void hda_codec_amp(hda_cmd_fn cmd, const hda_path_t *p, int volume, int mute, int enable) {
    int silent = (!enable) || mute || volume <= 0;

    for (int i = 0; i < p->nchain; i++) {
        int nid = p->chain[i];
        if (!(param(cmd, nid, PARAM_WIDGET_CAP) & WCAP_OUT_AMP)) continue;
        uint32_t ampcap = param(cmd, nid, PARAM_OUT_AMP_CAP);
        int steps = (int)((ampcap >> 8) & 0x7F);
        int mute_cap = (ampcap & 0x80000000u) != 0;

        int gain;
        if (silent) {
            gain = 0;
        } else if (nid == p->amp_nid) {
            gain = (p->amp_steps * volume) / 100;
            if (gain == 0) gain = 1;
        } else {
            gain = steps;
        }

        uint16_t payload = (uint16_t)(AMP_SET_OUT | AMP_SET_LEFT | AMP_SET_RIGHT |
                                      ((silent && mute_cap) ? AMP_MUTE : 0) |
                                      (gain & 0x7F));
        cmd(nid, VERB4(V_SET_AMP, payload));
    }
}

void hda_codec_disable(hda_cmd_fn cmd, const hda_path_t *p) {
    uint32_t ctl = cmd(p->nid, VERB12(V_GET_PINCTL, 0)) & 0xFF;
    ctl &= ~(PINCTL_OUT_EN | PINCTL_HP_EN);
    cmd(p->nid, VERB12(V_SET_PINCTL, ctl));
    if (p->has_eapd) cmd(p->nid, VERB12(V_SET_EAPD, 0x00));
    hda_codec_amp(cmd, p, 0, 1, 0);
}

void hda_codec_enable(hda_cmd_fn cmd, const hda_path_t *p, uint16_t fmt, int stream_tag) {
    for (int i = 0; i < p->nchain; i++) {
        int nid = p->chain[i];
        uint32_t cap = param(cmd, nid, PARAM_WIDGET_CAP);
        if (cap & WCAP_POWER) cmd(nid, VERB12(V_SET_POWER, 0x0));
        if (((cap >> 20) & 0xF) == WIDGET_SELECTOR && p->sel[i] >= 0)
            cmd(nid, VERB12(V_SET_CONNSEL, p->sel[i]));
    }

    uint32_t pincap = param(cmd, p->nid, PARAM_PIN_CAP);
    uint32_t ctl = PINCTL_OUT_EN;
    if (pincap & PINCAP_HP_DRIVE) ctl |= PINCTL_HP_EN;
    cmd(p->nid, VERB12(V_SET_PINCTL, ctl));
    if (p->has_eapd) cmd(p->nid, VERB12(V_SET_EAPD, 0x02));

    cmd(p->dac, VERB4(V_SET_FORMAT, fmt));
    cmd(p->dac, VERB12(V_SET_CONV, (stream_tag << 4) | 0x0));
}

int hda_codec_scan(hda_cmd_fn cmd, hda_path_t *out, int max, int *afg_out) {
    uint32_t root = param(cmd, 0, PARAM_NODE_COUNT);
    int fg_start = (root >> 16) & 0xFF;
    int fg_count = root & 0xFF;
    int n = 0;

    for (int fg = fg_start; fg < fg_start + fg_count && n == 0; fg++) {
        if ((param(cmd, fg, PARAM_FN_TYPE) & 0xFF) != 0x01) continue;
        if (afg_out) *afg_out = fg;
        cmd(fg, VERB12(V_SET_POWER, 0x0));

        uint32_t nc = param(cmd, fg, PARAM_NODE_COUNT);
        int w_start = (nc >> 16) & 0xFF;
        int w_count = nc & 0xFF;
        if (w_count > HDA_MAX_WIDGETS) w_count = HDA_MAX_WIDGETS;

        for (int nid = w_start; nid < w_start + w_count && n < max; nid++) {
            uint32_t cap = param(cmd, nid, PARAM_WIDGET_CAP);
            if (((cap >> 20) & 0xF) != WIDGET_PIN) continue;

            uint32_t pincap = param(cmd, nid, PARAM_PIN_CAP);
            uint32_t cfg = cmd(nid, VERB12(V_GET_CFGDEF, 0));
            int conn = (int)((cfg >> 30) & 0x3);
            int dev  = (int)((cfg >> 20) & 0xF);

            LOG_D("[hda] nid=0x%02x pincap=0x%08x cfg=0x%08x conn=%d dev=%d\n",
                  nid, pincap, cfg, conn, dev);

            if (!(pincap & PINCAP_OUTPUT)) continue;
            if (conn == CFG_CONN_NONE) continue;
            if (cap & WCAP_DIGITAL) continue;
            if (dev != DEV_LINE_OUT && dev != DEV_SPEAKER && dev != DEV_HP_OUT) continue;

            hda_path_t p;
            memset(&p, 0, sizeof p);
            p.nid = nid;
            p.dac = -1;
            if (find_dac(cmd, nid, 0, &p) != 0) {
                LOG_D("[hda] nid=0x%02x no DAC path\n", nid);
                continue;
            }
            p.kind = (dev == DEV_SPEAKER) ? AUDIO_OUT_SPEAKER
                   : (dev == DEV_HP_OUT)  ? AUDIO_OUT_HEADPHONE
                                          : AUDIO_OUT_LINEOUT;
            p.has_presence = (pincap & PINCAP_PRESENCE) && (conn != CFG_CONN_FIXED);
            p.has_eapd     = (pincap & PINCAP_EAPD) != 0;
            pick_amp(cmd, &p);
            out[n++] = p;

            LOG_I("[hda] out%d %s pin=0x%02x dac=0x%02x hops=%d amp=0x%02x steps=%d\n",
                  n - 1, hda_kind_name(p.kind), p.nid, p.dac, p.nchain,
                  p.amp_nid < 0 ? 0 : p.amp_nid, p.amp_steps);
        }
    }
    return n;
}
