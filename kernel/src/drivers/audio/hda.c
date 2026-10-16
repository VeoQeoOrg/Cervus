#include "../../../include/drivers/pci.h"
#include "../../../include/drivers/audio/hda.h"
#include "../../../include/drivers/audio/audio.h"
#include "../../../include/drivers/audio/hda_codec.h"
#include "../../../include/memory/dma.h"
#include "../../../include/sched/sched.h"
#include "../../../include/io/serial.h"
#include "../../../include/time/clocksource.h"
#include <string.h>
#include <stdbool.h>

#define HDA_GCAP        0x00
#define HDA_GCTL        0x08
#define HDA_STATESTS    0x0E
#define HDA_INTCTL      0x20
#define HDA_CORBLBASE   0x40
#define HDA_CORBUBASE   0x44
#define HDA_CORBWP      0x48
#define HDA_CORBRP      0x4A
#define HDA_CORBCTL     0x4C
#define HDA_CORBSIZE    0x4E
#define HDA_RIRBLBASE   0x50
#define HDA_RIRBUBASE   0x54
#define HDA_RIRBWP      0x58
#define HDA_RINTCNT     0x5A
#define HDA_RIRBCTL     0x5C
#define HDA_RIRBSTS     0x5D
#define HDA_RIRBSIZE    0x5E
#define HDA_ICOI        0x60
#define HDA_ICII        0x64
#define HDA_ICIS        0x68

#define ICIS_ICB        (1u << 0)
#define ICIS_IRV        (1u << 1)

#define GCTL_CRST       (1u << 0)

#define CORBRP_RST      (1u << 15)
#define RIRBWP_RST      (1u << 15)
#define DMA_RUN         (1u << 1)
#define HDA_RING        256

#define PCI_HDA_TCSEL   0x44
#define PCI_HDA_CGCTL   0x48
#define CGCTL_MISCBDCGE (1u << 6)
#define PCI_HDA_DEVC    0x78
#define DEVC_NOSNOOP    (1u << 11)

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

    uint32_t *corb;   uintptr_t corb_phys;
    uint32_t *rirb;   uintptr_t rirb_phys;
    uint16_t  rirb_rp;
    int       ring;
    int       dead;
    int       timeouts;

    hda_bdl_t *bdl;   uintptr_t bdl_phys;
    uint8_t   *buf[HDA_NBUF];
    uintptr_t  buf_phys[HDA_NBUF];
    uintptr_t  sd_base;

    uint8_t   w;
    uint32_t  fill;
    uint64_t  queued;
    int       running;
} hda_t;

static hda_t g_hda;

static inline uint16_t r16(uint32_t o){ return *(volatile uint16_t *)(g_hda.mmio + o); }
static inline uint32_t r32(uint32_t o){ return *(volatile uint32_t *)(g_hda.mmio + o); }
static inline void w8(uint32_t o, uint8_t v){ *(volatile uint8_t *)(g_hda.mmio + o) = v; }
static inline void w16(uint32_t o, uint16_t v){ *(volatile uint16_t *)(g_hda.mmio + o) = v; }
static inline void w32(uint32_t o, uint32_t v){ *(volatile uint32_t *)(g_hda.mmio + o) = v; }

static inline uint8_t  sd_r8 (uint32_t o){ return *(volatile uint8_t  *)(g_hda.mmio + g_hda.sd_base + o); }
static inline uint32_t sd_r32(uint32_t o){ return *(volatile uint32_t *)(g_hda.mmio + g_hda.sd_base + o); }
static inline void sd_w8 (uint32_t o, uint8_t v){ *(volatile uint8_t  *)(g_hda.mmio + g_hda.sd_base + o) = v; }
static inline void sd_w16(uint32_t o, uint16_t v){ *(volatile uint16_t *)(g_hda.mmio + g_hda.sd_base + o) = v; }
static inline void sd_w32(uint32_t o, uint32_t v){ *(volatile uint32_t *)(g_hda.mmio + g_hda.sd_base + o) = v; }

static int wait_reg(uint32_t off, uint32_t mask, uint32_t want, uint64_t us) {
    uint64_t end = clocksource_now_ns() + us * 1000ULL;
    for (;;) {
        uint32_t v = (off == HDA_GCTL) ? r32(off) : r16(off);
        if ((v & mask) == want) return 1;
        if (clocksource_now_ns() >= end) return 0;
        asm volatile("pause");
    }
}

static void delay_us(uint64_t us) {
    uint64_t end = clocksource_now_ns() + us * 1000ULL;
    while (clocksource_now_ns() < end) asm volatile("pause");
}

static uint32_t cmd_immediate(uint32_t val, int *ok) {
    uint64_t end = clocksource_now_ns() + 1000000ULL;
    while (r16(HDA_ICIS) & ICIS_ICB) {
        if (clocksource_now_ns() >= end) { *ok = 0; return 0; }
        asm volatile("pause");
    }
    w16(HDA_ICIS, ICIS_IRV);
    w32(HDA_ICOI, val);
    w16(HDA_ICIS, ICIS_ICB);
    end = clocksource_now_ns() + 20000000ULL;
    for (;;) {
        uint16_t st = r16(HDA_ICIS);
        if ((st & ICIS_IRV) && !(st & ICIS_ICB)) {
            uint32_t resp = r32(HDA_ICII);
            w16(HDA_ICIS, ICIS_IRV);
            *ok = 1;
            return resp;
        }
        if (clocksource_now_ns() >= end) { *ok = 0; return 0; }
        asm volatile("pause");
    }
}

static uint32_t cmd_ring(uint32_t val, int *ok) {
    uint16_t wp = (uint16_t)((r16(HDA_CORBWP) + 1) % HDA_RING);
    g_hda.corb[wp] = val;
    asm volatile("sfence" ::: "memory");
    w16(HDA_CORBWP, wp);
    uint64_t end = clocksource_now_ns() + 20000000ULL;
    for (;;) {
        uint16_t hw = (uint16_t)(r16(HDA_RIRBWP) & 0xFF);
        while (g_hda.rirb_rp != hw) {
            g_hda.rirb_rp = (uint16_t)((g_hda.rirb_rp + 1) % HDA_RING);
            asm volatile("lfence" ::: "memory");
            uint32_t resp = g_hda.rirb[g_hda.rirb_rp * 2];
            uint32_t ex = g_hda.rirb[g_hda.rirb_rp * 2 + 1];
            if (ex & (1u << 4)) continue;
            if ((int)(ex & 0xF) != g_hda.cad) continue;
            w8(HDA_RIRBSTS, 0x05);
            *ok = 1;
            return resp;
        }
        if (clocksource_now_ns() >= end) { *ok = 0; return 0; }
        asm volatile("pause");
    }
}

static uint32_t hda_cmd(int nid, uint32_t verb) {
    if (g_hda.dead) return 0;
    uint32_t val = ((uint32_t)g_hda.cad << 28) | ((uint32_t)(nid & 0xFF) << 20) | verb;
    int ok = 0;
    uint32_t resp = g_hda.ring ? cmd_ring(val, &ok) : cmd_immediate(val, &ok);
    if (ok) { g_hda.timeouts = 0; return resp; }
    if (++g_hda.timeouts < 3) return 0;
    g_hda.timeouts = 0;
    if (g_hda.ring) {
        serial_printf_lvl(LOG_LEVEL_WARN, "[hda] no response through CORB/RIRB, switching to immediate commands\n");
        w8(HDA_CORBCTL, 0);
        w8(HDA_RIRBCTL, 0);
        g_hda.ring = 0;
        resp = cmd_immediate(val, &ok);
        if (ok) return resp;
    }
    serial_printf_lvl(LOG_LEVEL_WARN, "[hda] codec %d stopped answering, giving up on it\n", g_hda.cad);
    g_hda.dead = 1;
    return 0;
}

static int ring_init(void) {
    if (!g_hda.corb) g_hda.corb = dma_alloc_coherent_low(HDA_RING * 4, &g_hda.corb_phys);
    if (!g_hda.rirb) g_hda.rirb = dma_alloc_coherent_low(HDA_RING * 8, &g_hda.rirb_phys);
    if (!g_hda.corb || !g_hda.rirb) return 0;
    memset(g_hda.corb, 0, HDA_RING * 4);
    memset(g_hda.rirb, 0, HDA_RING * 8);

    w8(HDA_CORBCTL, 0);
    wait_reg(HDA_CORBCTL, DMA_RUN, 0, 1000);
    w32(HDA_CORBLBASE, (uint32_t)g_hda.corb_phys);
    w32(HDA_CORBUBASE, (uint32_t)(g_hda.corb_phys >> 32));
    w8(HDA_CORBSIZE, 0x02);
    w16(HDA_CORBWP, 0);
    w16(HDA_CORBRP, CORBRP_RST);
    wait_reg(HDA_CORBRP, CORBRP_RST, CORBRP_RST, 1000);
    w16(HDA_CORBRP, 0);
    if (!wait_reg(HDA_CORBRP, CORBRP_RST, 0, 1000)) return 0;

    w8(HDA_RIRBCTL, 0);
    wait_reg(HDA_RIRBCTL, DMA_RUN, 0, 1000);
    w32(HDA_RIRBLBASE, (uint32_t)g_hda.rirb_phys);
    w32(HDA_RIRBUBASE, (uint32_t)(g_hda.rirb_phys >> 32));
    w8(HDA_RIRBSIZE, 0x02);
    w16(HDA_RIRBWP, RIRBWP_RST);
    w16(HDA_RINTCNT, 1);
    g_hda.rirb_rp = 0;
    w8(HDA_RIRBSTS, 0x05);

    w8(HDA_CORBCTL, DMA_RUN);
    w8(HDA_RIRBCTL, DMA_RUN | 0x01);
    return 1;
}

static void stop_engines(void) {
    uint16_t gcap = r16(HDA_GCAP);
    int nstreams = ((gcap >> 12) & 0xF) + ((gcap >> 8) & 0xF) + ((gcap >> 3) & 0x1F);
    for (int i = 0; i < nstreams; i++) {
        volatile uint8_t *ctl = g_hda.mmio + 0x80 + i * 0x20;
        *ctl = (uint8_t)(*ctl & ~SDCTL_RUN);
    }
    w8(HDA_CORBCTL, 0);
    w8(HDA_RIRBCTL, 0);
    w32(HDA_INTCTL, 0);
    delay_us(100);
}

static int controller_reset(void) {
    stop_engines();
    w16(HDA_STATESTS, 0x7FFF);
    w32(HDA_GCTL, r32(HDA_GCTL) & ~GCTL_CRST);
    if (!wait_reg(HDA_GCTL, GCTL_CRST, 0, 100000)) return 0;
    delay_us(500);
    w32(HDA_GCTL, r32(HDA_GCTL) | GCTL_CRST);
    if (!wait_reg(HDA_GCTL, GCTL_CRST, GCTL_CRST, 100000)) return 0;
    delay_us(1000);
    for (int i = 0; i < 100 && !(r16(HDA_STATESTS) & 0x7FFF); i++) delay_us(1000);
    return 1;
}

static int intel_skl_plus(uint16_t did) {
    static const uint16_t ids[] = {
        0xa170, 0x9d70, 0x9d71, 0xa171, 0xa2f0, 0x9dc8, 0xa348, 0x02c8, 0x06c8, 0xa3f0,
        0x34c8, 0x3dc8, 0x38c8, 0x4dc8, 0xa0c8, 0x43c8, 0x3198, 0x5a98, 0x1a98, 0x4b55,
        0x4b58, 0x7ad0, 0x51c8, 0x51c9, 0x51ca, 0x51cb, 0x51cc, 0x51cd, 0x51ce, 0x51cf,
        0x54c8, 0x7a50, 0x7e28, 0x7728, 0x7f50, 0xa828, 0xe428, 0x4d28, 0x2812, 0xf1c8,
    };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) if (ids[i] == did) return 1;
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
    for (int i = 0; i < 1000; i++) { if (sd_r8(SD_CTL) & SDCTL_SRST) break; delay_us(10); }
    sd_w8(SD_CTL, 0);
    for (int i = 0; i < 1000; i++) { if (!(sd_r8(SD_CTL) & SDCTL_SRST)) break; delay_us(10); }
}

int hda_open(uint32_t rate) {
    if (!g_hda.present) return -1;
    if (rate < 8000) rate = 8000;
    if (rate > 96000) rate = 96000;
    g_hda.cur_fmt = hda_fmt(rate, 2);

    g_hda.running = 0;
    g_hda.w = 0;
    g_hda.fill = 0;
    g_hda.queued = 0;

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

static int hda_wait_slot(void) {
    uint32_t lpib = sd_r32(SD_LPIB);
    uint8_t play = (uint8_t)((lpib / HDA_BUFSZ) % HDA_NBUF);
    uint8_t inflight = (uint8_t)((g_hda.w - play + HDA_NBUF) % HDA_NBUF);
    return !(g_hda.running && inflight >= HDA_NBUF - 1);
}

long hda_write(const void *pcm, size_t bytes) {
    if (!g_hda.present) return -1;
    const uint8_t *src = pcm;
    size_t off = 0;

    while (off < bytes) {
        if (g_hda.fill == 0 && !hda_wait_slot()) {
            task_sleep_ms(2);
            continue;
        }

        size_t space = HDA_BUFSZ - g_hda.fill;
        size_t chunk = bytes - off;
        if (chunk > space) chunk = space;
        memcpy(g_hda.buf[g_hda.w] + g_hda.fill, src + off, chunk);
        g_hda.fill += (uint32_t)chunk;
        off += chunk;

        if (g_hda.fill == HDA_BUFSZ) {
            g_hda.fill = 0;
            g_hda.w = (uint8_t)((g_hda.w + 1) % HDA_NBUF);
            g_hda.queued++;
            if (!g_hda.running && g_hda.queued >= 2) hda_start();
        }
    }

    if (g_hda.queued >= 2) hda_start();
    return (long)off;
}

int hda_close(void) {
    if (!g_hda.present) return -1;

    if (g_hda.fill > 0) {
        memset(g_hda.buf[g_hda.w] + g_hda.fill, 0, HDA_BUFSZ - g_hda.fill);
        g_hda.fill = 0;
        g_hda.w = (uint8_t)((g_hda.w + 1) % HDA_NBUF);
        g_hda.queued++;
    }
    if (g_hda.queued > 0) hda_start();

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
    g_hda.fill = 0;
    g_hda.queued = 0;
    return 0;
}

static int hda_abort(void) {
    if (!g_hda.present) return -1;
    sd_w8(SD_CTL, (uint8_t)(sd_r8(SD_CTL) & ~SDCTL_RUN));
    sd_w8(SD_STS, 0x1C);
    g_hda.running = 0;
    g_hda.w = 0;
    g_hda.fill = 0;
    g_hda.queued = 0;
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
    .abort      = hda_abort,
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

    int intel = dev->vendor_id == 0x8086;
    int skl = intel && intel_skl_plus(dev->device_id);
    if (intel) {
        uint8_t tc = pci_config_read8(dev->segment, dev->bus, dev->device, dev->function, PCI_HDA_TCSEL);
        pci_config_write8(dev->segment, dev->bus, dev->device, dev->function, PCI_HDA_TCSEL, tc & ~0x07);
        uint16_t devc = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_HDA_DEVC);
        if (devc & DEVC_NOSNOOP)
            pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_HDA_DEVC, devc & ~DEVC_NOSNOOP);
    }
    uint32_t cg = 0;
    if (skl) {
        cg = pci_config_read32(dev->segment, dev->bus, dev->device, dev->function, PCI_HDA_CGCTL);
        pci_config_write32(dev->segment, dev->bus, dev->device, dev->function, PCI_HDA_CGCTL, cg & ~CGCTL_MISCBDCGE);
    }
    int reset_ok = controller_reset();
    if (skl)
        pci_config_write32(dev->segment, dev->bus, dev->device, dev->function, PCI_HDA_CGCTL, cg | CGCTL_MISCBDCGE);
    if (!reset_ok) { serial_printf("[hda] controller reset timed out\n"); return -1; }

    uint16_t statests = r16(HDA_STATESTS);
    g_hda.cad = -1;
    for (int i = 0; i < 15; i++) if (statests & (1u << i)) { g_hda.cad = i; break; }
    if (g_hda.cad < 0) { serial_printf("[hda] no codec found (statests=0x%x)\n", statests); return -1; }
    w16(HDA_STATESTS, statests);

    g_hda.dead = 0;
    g_hda.timeouts = 0;
    g_hda.ring = ring_init();
    if (!g_hda.ring) serial_printf_lvl(LOG_LEVEL_WARN, "[hda] CORB/RIRB unavailable, using immediate commands\n");

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

    for (int i = 0; i < g_hda.npaths; i++) hda_codec_disable(hda_cmd, &g_hda.paths[i]);
    if (g_hda.dead) { serial_printf("[hda] codec %d did not answer\n", g_hda.cad); return -1; }

    g_hda.present = 1;
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
