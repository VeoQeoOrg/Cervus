#include "../../../include/drivers/pci.h"
#include "../../../include/drivers/audio/ac97.h"
#include "../../../include/drivers/audio/audio.h"
#include "../../../include/memory/dma.h"
#include "../../../include/sched/sched.h"
#include "../../../include/io/ports.h"
#include "../../../include/io/serial.h"
#include <string.h>
#include <stdbool.h>

#define NAM_RESET       0x00
#define NAM_MASTER_VOL  0x02
#define NAM_HP_VOL      0x04
#define NAM_PCM_VOL     0x18
#define NAM_EXT_ID      0x28
#define NAM_EXT_CTRL    0x2A
#define NAM_PCM_RATE    0x2C

#define PO_BDBAR        0x10
#define PO_CIV          0x14
#define PO_LVI          0x15
#define PO_SR           0x16
#define PO_PICB         0x18
#define PO_CR           0x1B
#define GLOB_CNT        0x2C
#define GLOB_STA        0x30

#define CR_RPBM         0x01
#define CR_RR           0x02

#define SR_DCH          0x0001
#define SR_CELV         0x0002
#define SR_LVBCI        0x0004
#define SR_BCIS         0x0008
#define SR_FIFOE        0x0010
#define SR_CLEAR        (SR_LVBCI | SR_BCIS | SR_FIFOE)

#define EXT_VRA         0x0001
#define GLOB_CNT_COLD   0x02
#define GLOB_STA_PCR    0x0100

#define AC97_NBUF       32
#define AC97_RING       65536

typedef struct __attribute__((packed)) {
    uint32_t addr;
    uint16_t samples;
    uint16_t ctrl;
} ac97_bd_t;

typedef struct {
    int       present;
    uint16_t  nam;
    uint16_t  nabm;
    int       vra;
    int       vol_steps;
    int       volume;
    int       mute;

    ac97_bd_t *bdl;   uintptr_t bdl_phys;
    uint8_t   *ring;  uintptr_t ring_phys;
    uint32_t   period;
    int        running;
} ac97_t;

static ac97_t g_ac97;

static inline uint8_t  po_r8 (uint16_t off) { return inb(g_ac97.nabm + off); }
static inline uint16_t po_r16(uint16_t off) { return inw(g_ac97.nabm + off); }
static inline void po_w8 (uint16_t off, uint8_t v)  { outb(g_ac97.nabm + off, v); }
static inline void po_w16(uint16_t off, uint16_t v) { outw(g_ac97.nabm + off, v); }
static inline void po_w32(uint16_t off, uint32_t v) { outl(g_ac97.nabm + off, v); }
static inline uint16_t nam_r16(uint16_t off) { return inw(g_ac97.nam + off); }
static inline void nam_w16(uint16_t off, uint16_t v) { outw(g_ac97.nam + off, v); }

static void spin(int loops) { for (volatile int i = 0; i < loops; i++) { } }

static void po_reset(void) {
    po_w8(PO_CR, CR_RR);
    for (int i = 0; i < 1000; i++) {
        if (!(po_r8(PO_CR) & CR_RR)) break;
        spin(10000);
    }
}

static void ac97_apply_vol(void) {
    int steps = g_ac97.vol_steps;
    int atten = (steps * (100 - g_ac97.volume)) / 100;
    if (atten > steps) atten = steps;
    if (atten < 0) atten = 0;
    uint16_t v = (uint16_t)((atten << 8) | atten);
    if (g_ac97.mute || g_ac97.volume == 0) v |= 0x8000;
    nam_w16(NAM_MASTER_VOL, v);
    nam_w16(NAM_HP_VOL, v);
}

static int ac97_mixer_get(audio_mixer_t *m) {
    if (!g_ac97.present) return -1;
    memset(m, 0, sizeof *m);
    strncpy(m->driver, "ac97", sizeof m->driver - 1);
    m->volume   = g_ac97.volume;
    m->mute     = g_ac97.mute;
    m->noutputs = 1;
    m->current  = 0;
    strncpy(m->outputs[0].name, "Line Out", sizeof m->outputs[0].name - 1);
    m->outputs[0].kind = AUDIO_OUT_LINEOUT;
    m->outputs[0].present = 1;
    return 0;
}

static int ac97_set_volume(int pct) {
    if (!g_ac97.present) return -1;
    g_ac97.volume = pct;
    ac97_apply_vol();
    return 0;
}

static int ac97_set_mute(int mute) {
    if (!g_ac97.present) return -1;
    g_ac97.mute = mute;
    ac97_apply_vol();
    return 0;
}

static uint32_t ac97_fix_rate(uint32_t rate) {
    if (!g_ac97.vra) return 48000;
    if (rate < 8000) rate = 8000;
    if (rate > 48000) rate = 48000;
    return rate;
}

static int ac97_prepare(uint32_t rate, uint32_t *period, uint32_t *periods) {
    if (!g_ac97.present) return -1;
    uint32_t pb = *period & ~3u;
    if (pb < 256) pb = 256;
    if (pb * AC97_NBUF > AC97_RING) pb = AC97_RING / AC97_NBUF;

    po_w8(PO_CR, 0);
    po_reset();
    g_ac97.running = 0;
    if (g_ac97.vra) nam_w16(NAM_PCM_RATE, (uint16_t)rate);
    for (int i = 0; i < AC97_NBUF; i++) {
        g_ac97.bdl[i].addr    = (uint32_t)(g_ac97.ring_phys + (uint64_t)i * pb);
        g_ac97.bdl[i].samples = (uint16_t)(pb / 2);
        g_ac97.bdl[i].ctrl    = 0;
    }
    po_w32(PO_BDBAR, (uint32_t)g_ac97.bdl_phys);
    po_w8(PO_LVI, AC97_NBUF - 1);
    g_ac97.period = pb;
    *period = pb;
    *periods = AC97_NBUF;
    return 0;
}

static void ac97_trigger(int run) {
    if (!g_ac97.present) return;
    if (run) {
        po_w8(PO_LVI, (uint8_t)((po_r8(PO_CIV) + AC97_NBUF - 1) % AC97_NBUF));
        po_w16(PO_SR, SR_CLEAR);
        po_w8(PO_CR, CR_RPBM);
        g_ac97.running = 1;
    } else {
        po_w8(PO_CR, (uint8_t)(po_r8(PO_CR) & ~CR_RPBM));
        po_w16(PO_SR, SR_CLEAR);
        g_ac97.running = 0;
    }
}

static uint32_t ac97_pointer(void) {
    if (!g_ac97.period) return 0;
    uint8_t civ;
    uint16_t picb;
    for (int i = 0; i < 4; i++) {
        civ = po_r8(PO_CIV);
        picb = po_r16(PO_PICB);
        if (po_r8(PO_CIV) == civ) break;
    }
    uint32_t left = (uint32_t)picb * 2;
    if (left > g_ac97.period) left = g_ac97.period;
    return (uint32_t)(civ % AC97_NBUF) * g_ac97.period + (g_ac97.period - left);
}

static void ac97_tick(void) {
    if (!g_ac97.running) return;
    po_w8(PO_LVI, (uint8_t)((po_r8(PO_CIV) + AC97_NBUF - 1) % AC97_NBUF));
    if (po_r16(PO_SR) & SR_DCH) {
        po_w16(PO_SR, SR_CLEAR);
        po_w8(PO_CR, CR_RPBM);
    }
}

static int ac97_set_output(int idx) {
    if (!g_ac97.present) return -1;
    return (idx <= 0) ? 0 : -1;
}

static audio_backend_t g_ac97_backend = {
    .name        = "ac97",
    .ring_max    = AC97_RING,
    .max_periods = AC97_NBUF,
    .fix_rate    = ac97_fix_rate,
    .prepare     = ac97_prepare,
    .trigger     = ac97_trigger,
    .pointer     = ac97_pointer,
    .tick        = ac97_tick,
    .mixer_get   = ac97_mixer_get,
    .set_volume  = ac97_set_volume,
    .set_mute    = ac97_set_mute,
    .set_output  = ac97_set_output,
};

static int ac97_probe(pci_device_t *dev) {
    if (g_ac97.present) return -1;
    if (dev->bars[0].type != PCI_BAR_TYPE_IO || dev->bars[1].type != PCI_BAR_TYPE_IO ||
        !dev->bars[0].base || !dev->bars[1].base) {
        serial_printf("[ac97] BAR0/BAR1 not I/O, skipping\n");
        return -1;
    }

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);

    g_ac97.nam  = (uint16_t)dev->bars[0].base;
    g_ac97.nabm = (uint16_t)dev->bars[1].base;

    po_w32(GLOB_CNT, GLOB_CNT_COLD);
    spin(100000);

    nam_w16(NAM_RESET, 1);
    for (int i = 0; i < 1000; i++) {
        if (inl(g_ac97.nabm + GLOB_STA) & GLOB_STA_PCR) break;
        spin(10000);
    }

    nam_w16(NAM_MASTER_VOL, 0x2020);
    g_ac97.vol_steps = (nam_r16(NAM_MASTER_VOL) & 0x20) ? 0x3F : 0x1F;
    g_ac97.volume = 75;
    g_ac97.mute = 0;
    ac97_apply_vol();
    nam_w16(NAM_PCM_VOL, 0x0808);

    uint16_t ext = nam_r16(NAM_EXT_ID);
    if (ext & EXT_VRA) {
        nam_w16(NAM_EXT_CTRL, nam_r16(NAM_EXT_CTRL) | EXT_VRA);
        g_ac97.vra = 1;
    }

    g_ac97.bdl = dma_alloc_coherent_low(sizeof(ac97_bd_t) * AC97_NBUF, &g_ac97.bdl_phys);
    if (!g_ac97.bdl) { serial_printf("[ac97] BDL alloc failed\n"); return -1; }
    g_ac97.ring = dma_alloc_coherent_low(AC97_RING, &g_ac97.ring_phys);
    if (!g_ac97.ring) { serial_printf("[ac97] ring alloc failed\n"); return -1; }
    g_ac97_backend.ring = g_ac97.ring;

    po_reset();
    g_ac97.present = 1;
    audio_register(&g_ac97_backend);
    serial_printf("[ac97] audio %04x:%04x NAM=0x%x NABM=0x%x vra=%d volsteps=%d\n",
                  dev->vendor_id, dev->device_id, g_ac97.nam, g_ac97.nabm,
                  g_ac97.vra, g_ac97.vol_steps);
    return 0;
}

static const pci_driver_t g_ac97_driver = {
    .name           = "ac97",
    .match_vendor   = -1,
    .match_device   = -1,
    .match_class    = 0x04,
    .match_subclass = 0x01,
    .probe          = ac97_probe,
};

void ac97_init(void) {
    pci_register_driver(&g_ac97_driver);
}
