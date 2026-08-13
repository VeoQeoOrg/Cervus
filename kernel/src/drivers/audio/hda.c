#include "../../../include/drivers/pci.h"
#include "../../../include/drivers/audio/hda.h"
#include "../../../include/drivers/audio/audio.h"
#include "../../../include/memory/dma.h"
#include "../../../include/sched/sched.h"
#include "../../../include/io/serial.h"
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
#define CORBCTL_RUN     (1u << 1)
#define RIRBCTL_DMAEN   (1u << 1)

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

#define VERB12(v, p)  (((uint32_t)(v) << 8) | ((uint32_t)(p) & 0xFF))
#define VERB4(v, p)   (((uint32_t)(v) << 16) | ((uint32_t)(p) & 0xFFFF))

#define V_GET_PARAM     0xF00
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

#define WIDGET_DAC          0x0
#define WIDGET_PIN          0x4

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
    int       dac_nid;
    int       pin_nid;
    int       out_sd;
    int       stream_tag;

    hda_bdl_t *bdl;   uintptr_t bdl_phys;
    uint8_t   *buf[HDA_NBUF];
    uintptr_t  buf_phys[HDA_NBUF];
    uintptr_t  sd_base;

    uint8_t   w;
    int       running;
} hda_t;

static hda_t g_hda;

static inline uint8_t  r8 (uint32_t o){ return *(volatile uint8_t  *)(g_hda.mmio + o); }
static inline uint16_t r16(uint32_t o){ return *(volatile uint16_t *)(g_hda.mmio + o); }
static inline uint32_t r32(uint32_t o){ return *(volatile uint32_t *)(g_hda.mmio + o); }
static inline void w8 (uint32_t o, uint8_t v){ *(volatile uint8_t  *)(g_hda.mmio + o) = v; }
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

static uint32_t hda_param(int nid, uint8_t param) {
    return hda_cmd(nid, VERB12(V_GET_PARAM, param));
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

static int hda_walk_codec(void) {
    uint32_t root = hda_param(0, PARAM_NODE_COUNT);
    int fg_start = (root >> 16) & 0xFF;
    int fg_count = root & 0xFF;

    for (int fg = fg_start; fg < fg_start + fg_count; fg++) {
        uint32_t ft = hda_param(fg, PARAM_FN_TYPE);
        if ((ft & 0xFF) != 0x01) continue;

        hda_cmd(fg, VERB12(V_SET_POWER, 0x0));

        uint32_t nc = hda_param(fg, PARAM_NODE_COUNT);
        int w_start = (nc >> 16) & 0xFF;
        int w_count = nc & 0xFF;

        for (int nid = w_start; nid < w_start + w_count; nid++) {
            uint32_t cap = hda_param(nid, PARAM_WIDGET_CAP);
            int type = (cap >> 20) & 0xF;
            if (type == WIDGET_DAC && g_hda.dac_nid < 0) g_hda.dac_nid = nid;
            else if (type == WIDGET_PIN && g_hda.pin_nid < 0) g_hda.pin_nid = nid;
        }
        if (g_hda.dac_nid >= 0 && g_hda.pin_nid >= 0) return 0;
    }
    return -1;
}

static void hda_setup_path(uint16_t fmt) {
    hda_cmd(g_hda.dac_nid, VERB12(V_SET_POWER, 0x0));
    hda_cmd(g_hda.dac_nid, VERB4(V_SET_FORMAT, fmt));
    hda_cmd(g_hda.dac_nid, VERB12(V_SET_CONV, (g_hda.stream_tag << 4) | 0x0));
    hda_cmd(g_hda.dac_nid, VERB4(V_SET_AMP, 0xB07F));

    hda_cmd(g_hda.pin_nid, VERB12(V_SET_POWER, 0x0));
    hda_cmd(g_hda.pin_nid, VERB12(V_SET_CONNSEL, 0x0));
    hda_cmd(g_hda.pin_nid, VERB12(V_SET_PINCTL, 0xC0));
    hda_cmd(g_hda.pin_nid, VERB12(V_SET_EAPD, 0x02));
    hda_cmd(g_hda.pin_nid, VERB4(V_SET_AMP, 0xB07F));
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
    uint16_t fmt = hda_fmt(rate, 2);

    g_hda.running = 0;
    g_hda.w = 0;

    sd_w8(SD_CTL, 0);
    stream_reset();

    for (int i = 0; i < HDA_NBUF; i++) memset(g_hda.buf[i], 0, HDA_BUFSZ);

    sd_w32(SD_BDLPL, (uint32_t)(g_hda.bdl_phys & 0xFFFFFFFF));
    sd_w32(SD_BDLPU, (uint32_t)(g_hda.bdl_phys >> 32));
    sd_w32(SD_CBL, HDA_NBUF * HDA_BUFSZ);
    sd_w16(SD_LVI, HDA_NBUF - 1);
    sd_w16(SD_FMT, fmt);
    sd_w8(SD_CTL + 2, (uint8_t)(g_hda.stream_tag << 4));

    hda_setup_path(fmt);
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

static const audio_backend_t g_hda_backend = {
    .name  = "hda",
    .open  = hda_open,
    .write = hda_write,
    .close = hda_close,
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

    g_hda.dac_nid = -1;
    g_hda.pin_nid = -1;
    if (hda_walk_codec() != 0) {
        serial_printf("[hda] no DAC/pin path (dac=%d pin=%d)\n", g_hda.dac_nid, g_hda.pin_nid);
        return -1;
    }

    uint16_t gcap = r16(HDA_GCAP);
    int iss = (gcap >> 8) & 0xF;
    g_hda.out_sd = iss;
    g_hda.sd_base = 0x80 + (uintptr_t)g_hda.out_sd * 0x20;
    g_hda.stream_tag = 1;

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
    audio_register(&g_hda_backend);
    serial_printf("[hda] %04x:%04x codec=%d dac=%d pin=%d out_sd=%d\n",
                  dev->vendor_id, dev->device_id, g_hda.cad, g_hda.dac_nid, g_hda.pin_nid, g_hda.out_sd);
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
