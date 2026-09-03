#include "../../../include/drivers/pci.h"
#include "../../../include/drivers/audio/hda.h"
#include "../../../include/drivers/audio/audio.h"
#include "../../../include/drivers/audio/hda_codec.h"
#include "../../../include/memory/dma.h"
#include "../../../include/sched/sched.h"
#include "../../../include/io/serial.h"
#include <string.h>
#include <stdbool.h>

#define HDA_GCAP        0x00
#define HDA_GCTL        0x08
#define HDA_STATESTS    0x0E
#define HDA_INTCTL      0x20
#define HDA_ICOI        0x60
#define HDA_ICII        0x64
#define HDA_ICIS        0x68

#define ICIS_ICB        (1u << 0)
#define ICIS_IRV        (1u << 1)

#define GCTL_CRST       (1u << 0)

#define SD_CTL          0x00
#define SD_STS          0x03
#define SD_LPIB         0x04
#define SD_CBL          0x08
#define SD_LVI          0x0C
#define SD_FMT          0x12
#define SD_BDLPL        0x18
#define SD_BDLPU        0x1C

#define SDCTL_SRST      (1u << 0)
#define SDCTL_RUN       (1u << 1)

#define HDA_NBUF        16
#define HDA_BUFSZ       4096

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint32_t flags;
} hda_bdl_t;

typedef struct {
    int      present;
    volatile uint8_t *mmio;

    int       cad;
    int       afg;
    int       out_sd;
    int       stream_tag;
    uint16_t  cur_fmt;

    hda_path_t paths[AUDIO_MAX_OUTPUTS];
    int        npaths;
    int        cur_path;
    int        auto_out;

    int       volume;
    int       mute;

    hda_bdl_t *bdl;   uintptr_t bdl_phys;
    uint8_t   *buf[HDA_NBUF];
    uintptr_t  buf_phys[HDA_NBUF];
    uintptr_t  sd_base;

    uint8_t   w;
    int       running;
} hda_t;

static hda_t g_hda;

static inline uint16_t r16(uint32_t o){ return *(volatile uint16_t *)(g_hda.mmio + o); }
static inline uint32_t r32(uint32_t o){ return *(volatile uint32_t *)(g_hda.mmio + o); }
static inline void w16(uint32_t o, uint16_t v){ *(volatile uint16_t *)(g_hda.mmio + o) = v; }
static inline void w32(uint32_t o, uint32_t v){ *(volatile uint32_t *)(g_hda.mmio + o) = v; }

static inline uint8_t  sd_r8 (uint32_t o){ return *(volatile uint8_t  *)(g_hda.mmio + g_hda.sd_base + o); }
static inline uint32_t sd_r32(uint32_t o){ return *(volatile uint32_t *)(g_hda.mmio + g_hda.sd_base + o); }
static inline void sd_w8 (uint32_t o, uint8_t v){ *(volatile uint8_t  *)(g_hda.mmio + g_hda.sd_base + o) = v; }
static inline void sd_w16(uint32_t o, uint16_t v){ *(volatile uint16_t *)(g_hda.mmio + g_hda.sd_base + o) = v; }
static inline void sd_w32(uint32_t o, uint32_t v){ *(volatile uint32_t *)(g_hda.mmio + g_hda.sd_base + o) = v; }

static void spin(int n){ for (volatile int i=0;i<n;i++){} }

static uint32_t hda_cmd(int nid, uint32_t verb) {
    uint32_t val = ((uint32_t)g_hda.cad << 28) | ((uint32_t)nid << 20) | verb;

    for (int i = 0; i < 1000 && (r16(HDA_ICIS) & ICIS_ICB); i++) spin(100);
    w16(HDA_ICIS, ICIS_IRV);
    w32(HDA_ICOI, val);
    w16(HDA_ICIS, ICIS_ICB);

    for (int i = 0; i < 100000; i++) {
        uint16_t s = r16(HDA_ICIS);
        if ((s & ICIS_IRV) && !(s & ICIS_ICB)) {
            uint32_t resp = r32(HDA_ICII);
            w16(HDA_ICIS, ICIS_IRV);
            return resp;
        }
        spin(100);
    }
    return 0;
}



static uint16_t hda_fmt(uint32_t rate, int channels) {
    uint16_t base = 0, mult = 0, div = 0;
    switch (rate) {
        case 48000: break;
        case 44100: base = 1; break;
        case 96000: mult = 1; break;
        case 88200: base = 1; mult = 1; break;
        case 32000: mult = 1; div = 2; break;
        case 24000: div = 1; break;
        case 22050: base = 1; div = 1; break;
        case 16000: div = 2; break;
        case 11025: base = 1; div = 3; break;
        case 8000:  div = 5; break;
        default: break;
    }
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;
    return (uint16_t)((base << 14) | (mult << 11) | (div << 8) | (0x1 << 4) | ((channels - 1) & 0xF));
}








static int auto_pick(void) {
    int best = -1, best_rank = -1;
    for (int i = 0; i < g_hda.npaths; i++) {
        if (!hda_codec_present(hda_cmd, &g_hda.paths[i])) continue;
        int rank;
        switch (g_hda.paths[i].kind) {
            case AUDIO_OUT_HEADPHONE: rank = 3; break;
            case AUDIO_OUT_LINEOUT:   rank = 2; break;
            case AUDIO_OUT_SPEAKER:   rank = 1; break;
            default:                  rank = 0; break;
        }
        if (rank > best_rank) { best_rank = rank; best = i; }
    }
    if (best < 0 && g_hda.npaths > 0) best = 0;
    return best;
}

static void apply_current(void) {
    int want = g_hda.auto_out ? auto_pick() : g_hda.cur_path;
    if (want < 0) return;
    for (int i = 0; i < g_hda.npaths; i++)
        if (i != want) hda_codec_disable(hda_cmd, &g_hda.paths[i]);
    g_hda.cur_path = want;
    hda_codec_enable(hda_cmd, &g_hda.paths[want], g_hda.cur_fmt, g_hda.stream_tag);
    hda_codec_amp(hda_cmd, &g_hda.paths[want], g_hda.volume, g_hda.mute, 1);
}


static void stream_reset(void) {
    sd_w8(SD_CTL, SDCTL_SRST);
    for (int i = 0; i < 1000; i++) { if (sd_r8(SD_CTL) & SDCTL_SRST) break; spin(1000); }
    sd_w8(SD_CTL, 0);
    for (int i = 0; i < 1000; i++) { if (!(sd_r8(SD_CTL) & SDCTL_SRST)) break; spin(1000); }
}

int hda_open(uint32_t rate) {
    if (!g_hda.present) return -1;
    if (rate < 8000) rate = 8000;
    if (rate > 96000) rate = 96000;
    g_hda.cur_fmt = hda_fmt(rate, 2);

    g_hda.running = 0;
    g_hda.w = 0;

    sd_w8(SD_CTL, 0);
    stream_reset();

    for (int i = 0; i < HDA_NBUF; i++) memset(g_hda.buf[i], 0, HDA_BUFSZ);

    sd_w32(SD_BDLPL, (uint32_t)(g_hda.bdl_phys & 0xFFFFFFFF));
    sd_w32(SD_BDLPU, (uint32_t)(g_hda.bdl_phys >> 32));
    sd_w32(SD_CBL, HDA_NBUF * HDA_BUFSZ);
    sd_w16(SD_LVI, HDA_NBUF - 1);
    sd_w16(SD_FMT, g_hda.cur_fmt);
    sd_w8(SD_CTL + 2, (uint8_t)(g_hda.stream_tag << 4));

    apply_current();
    return 0;
}

static void hda_start(void) {
    if (g_hda.running) return;
    sd_w8(SD_STS, 0x1C);
    sd_w8(SD_CTL, (uint8_t)(sd_r8(SD_CTL) | SDCTL_RUN));
    g_hda.running = 1;
}

long hda_write(const void *pcm, size_t bytes) {
    if (!g_hda.present) return -1;
    const uint8_t *src = pcm;
    size_t off = 0;

    while (off < bytes) {
        uint32_t lpib = sd_r32(SD_LPIB);
        uint8_t play = (uint8_t)((lpib / HDA_BUFSZ) % HDA_NBUF);
        uint8_t inflight = (uint8_t)((g_hda.w - play + HDA_NBUF) % HDA_NBUF);
        if (g_hda.running && inflight >= HDA_NBUF - 1) {
            task_sleep_ms(2);
            continue;
        }

        size_t chunk = bytes - off;
        if (chunk > HDA_BUFSZ) chunk = HDA_BUFSZ;
        memcpy(g_hda.buf[g_hda.w], src + off, chunk);
        if (chunk < HDA_BUFSZ) memset(g_hda.buf[g_hda.w] + chunk, 0, HDA_BUFSZ - chunk);

        g_hda.w = (uint8_t)((g_hda.w + 1) % HDA_NBUF);
        off += chunk;

        if (!g_hda.running && g_hda.w >= 2) hda_start();
    }

    hda_start();
    return (long)off;
}

int hda_close(void) {
    if (!g_hda.present) return -1;
    if (g_hda.running) {
        for (int guard = 0; guard < 20000; guard++) {
            uint32_t lpib = sd_r32(SD_LPIB);
            uint8_t play = (uint8_t)((lpib / HDA_BUFSZ) % HDA_NBUF);
            uint8_t inflight = (uint8_t)((g_hda.w - play + HDA_NBUF) % HDA_NBUF);
            if (inflight <= 1) break;
            task_sleep_ms(5);
        }
        sd_w8(SD_CTL, (uint8_t)(sd_r8(SD_CTL) & ~SDCTL_RUN));
        stream_reset();
    }
    g_hda.running = 0;
    g_hda.w = 0;
    return 0;
}

static int hda_mixer_get(audio_mixer_t *m) {
    if (!g_hda.present) return -1;
    memset(m, 0, sizeof *m);
    strncpy(m->driver, "hda", sizeof m->driver - 1);
    m->volume = g_hda.volume;
    m->mute = g_hda.mute;
    m->noutputs = g_hda.npaths;
    m->current = g_hda.auto_out ? -1 : g_hda.cur_path;
    for (int i = 0; i < g_hda.npaths && i < AUDIO_MAX_OUTPUTS; i++) {
        strncpy(m->outputs[i].name, hda_kind_name(g_hda.paths[i].kind),
                sizeof m->outputs[i].name - 1);
        m->outputs[i].kind = (uint8_t)g_hda.paths[i].kind;
        m->outputs[i].present = (uint8_t)hda_codec_present(hda_cmd, &g_hda.paths[i]);
    }
    return 0;
}

static int hda_set_volume(int pct) {
    if (!g_hda.present) return -1;
    g_hda.volume = pct;
    if (g_hda.cur_path >= 0)
        hda_codec_amp(hda_cmd, &g_hda.paths[g_hda.cur_path], g_hda.volume, g_hda.mute, 1);
    return 0;
}

static int hda_set_mute(int mute) {
    if (!g_hda.present) return -1;
    g_hda.mute = mute;
    if (g_hda.cur_path >= 0)
        hda_codec_amp(hda_cmd, &g_hda.paths[g_hda.cur_path], g_hda.volume, g_hda.mute, 1);
    return 0;
}

static int hda_set_output(int idx) {
    if (!g_hda.present) return -1;
    if (idx < 0) {
        g_hda.auto_out = 1;
    } else {
        if (idx >= g_hda.npaths) return -1;
        g_hda.auto_out = 0;
        g_hda.cur_path = idx;
    }
    apply_current();
    return 0;
}

static const audio_backend_t g_hda_backend = {
    .name       = "hda",
    .open       = hda_open,
    .write      = hda_write,
    .close      = hda_close,
    .mixer_get  = hda_mixer_get,
    .set_volume = hda_set_volume,
    .set_mute   = hda_set_mute,
    .set_output = hda_set_output,
};

static int hda_probe(pci_device_t *dev) {
    if (g_hda.present) return -1;
    if (dev->bars[0].type != PCI_BAR_TYPE_MEM || !dev->bars[0].base) {
        serial_printf("[hda] BAR0 not MMIO, skipping\n");
        return -1;
    }

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);

    g_hda.mmio = (volatile uint8_t *)mmio_map(dev->bars[0].base, dev->bars[0].size);
    if (!g_hda.mmio) return -1;

    w32(HDA_GCTL, r32(HDA_GCTL) & ~GCTL_CRST);
    for (int i = 0; i < 1000; i++) { if (!(r32(HDA_GCTL) & GCTL_CRST)) break; spin(1000); }
    w32(HDA_GCTL, r32(HDA_GCTL) | GCTL_CRST);
    for (int i = 0; i < 1000; i++) { if (r32(HDA_GCTL) & GCTL_CRST) break; spin(1000); }
    spin(500000);

    uint16_t statests = r16(HDA_STATESTS);
    g_hda.cad = -1;
    for (int i = 0; i < 15; i++) if (statests & (1u << i)) { g_hda.cad = i; break; }
    if (g_hda.cad < 0) { serial_printf("[hda] no codec found (statests=0x%x)\n", statests); return -1; }

    g_hda.npaths = 0;
    g_hda.cur_path = -1;
    g_hda.auto_out = 1;
    g_hda.volume = 75;
    g_hda.mute = 0;
    g_hda.stream_tag = 1;
    g_hda.cur_fmt = hda_fmt(44100, 2);

    g_hda.npaths = hda_codec_scan(hda_cmd, g_hda.paths, AUDIO_MAX_OUTPUTS, &g_hda.afg);
    if (g_hda.npaths <= 0) {
        serial_printf("[hda] no usable output pin found on codec %d\n", g_hda.cad);
        return -1;
    }

    uint16_t gcap = r16(HDA_GCAP);
    int iss = (gcap >> 8) & 0xF;
    g_hda.out_sd = iss;
    g_hda.sd_base = 0x80 + (uintptr_t)g_hda.out_sd * 0x20;

    g_hda.bdl = dma_alloc_coherent_low(sizeof(hda_bdl_t) * HDA_NBUF, &g_hda.bdl_phys);
    if (!g_hda.bdl) { serial_printf("[hda] BDL alloc failed\n"); return -1; }
    memset(g_hda.bdl, 0, sizeof(hda_bdl_t) * HDA_NBUF);
    for (int i = 0; i < HDA_NBUF; i++) {
        g_hda.buf[i] = dma_alloc_coherent_low(HDA_BUFSZ, &g_hda.buf_phys[i]);
        if (!g_hda.buf[i]) { serial_printf("[hda] buffer alloc failed\n"); return -1; }
        g_hda.bdl[i].addr = g_hda.buf_phys[i];
        g_hda.bdl[i].len = HDA_BUFSZ;
        g_hda.bdl[i].flags = 0;
    }

    g_hda.present = 1;
    apply_current();
    audio_register(&g_hda_backend);
    serial_printf("[hda] %04x:%04x codec=%d outputs=%d out_sd=%d\n",
                  dev->vendor_id, dev->device_id, g_hda.cad, g_hda.npaths, g_hda.out_sd);
    return 0;
}

static const pci_driver_t g_hda_driver = {
    .name           = "hda",
    .match_vendor   = -1,
    .match_device   = -1,
    .match_class    = 0x04,
    .match_subclass = 0x03,
    .probe          = hda_probe,
};

void hda_init(void) {
    pci_register_driver(&g_hda_driver);
}
