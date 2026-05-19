#include "../../include/drivers/ahci.h"
#include "../../include/drivers/pci.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/vmm.h"
#include "../../include/memory/paging.h"
#include "../../include/sched/spinlock.h"
#include "../../include/syscall/errno.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define HBA_CAP        0x00
#define HBA_GHC        0x04
#define HBA_IS         0x08
#define HBA_PI         0x0C
#define HBA_VS         0x10
#define HBA_CAP2       0x24
#define HBA_BOHC       0x28

#define GHC_HR         (1u << 0)
#define GHC_IE         (1u << 1)
#define GHC_AE         (1u << 31)

#define CAP_S64A       (1u << 31)
#define CAP_NCS_SHIFT  8
#define CAP_NCS_MASK   0x1F

#define PORT_BASE(p)   (0x100u + (p) * 0x80u)
#define PRT_CLB        0x00
#define PRT_CLBU       0x04
#define PRT_FB         0x08
#define PRT_FBU        0x0C
#define PRT_IS         0x10
#define PRT_IE         0x14
#define PRT_CMD        0x18
#define PRT_TFD        0x20
#define PRT_SIG        0x24
#define PRT_SSTS       0x28
#define PRT_SCTL       0x2C
#define PRT_SERR       0x30
#define PRT_SACT       0x34
#define PRT_CI         0x38
#define PRT_SNTF       0x3C
#define PRT_FBS        0x40

#define PXCMD_ST       (1u << 0)
#define PXCMD_SUD      (1u << 1)
#define PXCMD_POD      (1u << 2)
#define PXCMD_FRE      (1u << 4)
#define PXCMD_FR       (1u << 14)
#define PXCMD_CR       (1u << 15)
#define PXCMD_ICC_ACTIVE (1u << 28)

#define SSTS_DET_MASK  0x0F
#define SSTS_DET_PRESENT 0x03
#define SSTS_IPM_MASK  0xF00
#define SSTS_IPM_ACTIVE 0x100

#define SIG_SATA       0x00000101u
#define SIG_ATAPI      0xEB140101u
#define SIG_SEMB       0xC33C0101u
#define SIG_PM         0x96690101u

#define ATA_BSY        0x80
#define ATA_DRQ        0x08
#define ATA_ERR        0x01

#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_READ_DMA_EX     0x25
#define ATA_CMD_WRITE_DMA_EX    0x35
#define ATA_CMD_FLUSH_CACHE_EX  0xEA

#define FIS_TYPE_REG_H2D 0x27

typedef struct {
    uint16_t flags;
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv[4];
} __attribute__((packed)) ahci_cmd_header_t;

typedef struct {
    uint8_t  fis_type;
    uint8_t  pm;
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0, lba1, lba2;
    uint8_t  device;
    uint8_t  lba3, lba4, lba5;
    uint8_t  featureh;
    uint16_t count;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv1[4];
} __attribute__((packed)) ahci_fis_reg_h2d_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc;
} __attribute__((packed)) ahci_prdt_entry_t;

typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    ahci_prdt_entry_t prdt[8];
} __attribute__((packed)) ahci_cmd_table_t;

static ahci_hba_t g_hba;
static int       g_hba_present = 0;
static ahci_device_t *g_devs[AHCI_MAX_DEVICES];
static int       g_dev_count = 0;
static spinlock_t g_ahci_lock = SPINLOCK_INIT;

static inline uint32_t hba_r32(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}
static inline void hba_w32(volatile uint8_t *base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(base + off) = v;
}
static inline uint32_t prt_r32(volatile uint8_t *base, int port, uint32_t off) {
    return hba_r32(base, PORT_BASE(port) + off);
}
static inline void prt_w32(volatile uint8_t *base, int port, uint32_t off, uint32_t v) {
    hba_w32(base, PORT_BASE(port) + off, v);
}

static void io_pause(void) {
    asm volatile("pause" ::: "memory");
}

static int ahci_wait_clear32(volatile uint8_t *base, uint32_t off, uint32_t mask,
                             uint32_t timeout_loops) {
    for (uint32_t i = 0; i < timeout_loops; i++) {
        if ((hba_r32(base, off) & mask) == 0) return 0;
        for (int k = 0; k < 64; k++) io_pause();
    }
    return -ETIMEDOUT;
}

static int ahci_port_wait_clear(volatile uint8_t *base, int port, uint32_t off,
                                uint32_t mask, uint32_t timeout_loops) {
    return ahci_wait_clear32(base, PORT_BASE(port) + off, mask, timeout_loops);
}

static void ahci_port_stop(volatile uint8_t *base, int port) {
    uint32_t cmd = prt_r32(base, port, PRT_CMD);
    cmd &= ~PXCMD_ST;
    prt_w32(base, port, PRT_CMD, cmd);
    ahci_port_wait_clear(base, port, PRT_CMD, PXCMD_CR, 500000);
    cmd = prt_r32(base, port, PRT_CMD);
    cmd &= ~PXCMD_FRE;
    prt_w32(base, port, PRT_CMD, cmd);
    ahci_port_wait_clear(base, port, PRT_CMD, PXCMD_FR, 500000);
}

static void ahci_port_start(volatile uint8_t *base, int port) {
    ahci_port_wait_clear(base, port, PRT_CMD, PXCMD_CR, 500000);
    uint32_t cmd = prt_r32(base, port, PRT_CMD);
    cmd |= PXCMD_FRE;
    prt_w32(base, port, PRT_CMD, cmd);
    cmd |= PXCMD_ST;
    prt_w32(base, port, PRT_CMD, cmd);
}

static int find_cmd_slot(volatile uint8_t *base, int port, uint8_t ncs) {
    uint32_t slots = prt_r32(base, port, PRT_SACT) | prt_r32(base, port, PRT_CI);
    for (int i = 0; i < ncs; i++) {
        if ((slots & (1u << i)) == 0) return i;
    }
    return -1;
}

static void ahci_setup_port(ahci_hba_t *hba, int port, ahci_device_t *d) {
    volatile uint8_t *base = hba->abar;

    ahci_port_stop(base, port);

    void *page = pmm_alloc_zero(3);
    if (!page) {
        serial_writestring("[ahci] failed to alloc port pages\n");
        return;
    }
    uintptr_t phys = pmm_virt_to_phys(page);

    d->clb_virt   = page;
    d->clb_phys   = phys;
    d->fb_virt    = (uint8_t *)page + 1024;
    d->fb_phys    = phys + 1024;
    d->ctba_virt  = (uint8_t *)page + 4096;
    d->ctba_phys  = phys + 4096;

    ahci_cmd_header_t *hdr = (ahci_cmd_header_t *)d->clb_virt;
    for (int i = 0; i < 32; i++) {
        memset(&hdr[i], 0, sizeof(hdr[i]));
        uintptr_t ct = d->ctba_phys + (uintptr_t)i * 256;
        hdr[i].ctba  = (uint32_t)(ct & 0xFFFFFFFFu);
        hdr[i].ctbau = (uint32_t)(ct >> 32);
    }

    prt_w32(base, port, PRT_CLB,  (uint32_t)(d->clb_phys & 0xFFFFFFFFu));
    prt_w32(base, port, PRT_CLBU, (uint32_t)(d->clb_phys >> 32));
    prt_w32(base, port, PRT_FB,   (uint32_t)(d->fb_phys  & 0xFFFFFFFFu));
    prt_w32(base, port, PRT_FBU,  (uint32_t)(d->fb_phys  >> 32));

    prt_w32(base, port, PRT_SERR, 0xFFFFFFFFu);
    prt_w32(base, port, PRT_IS,   0xFFFFFFFFu);
    prt_w32(base, port, PRT_IE,   0);

    ahci_port_start(base, port);
}

static int ahci_issue_cmd(ahci_hba_t *hba, ahci_device_t *d, int slot,
                          uint32_t timeout_loops) {
    volatile uint8_t *base = hba->abar;
    int port = d->port_idx;

    for (uint32_t i = 0; i < 1000000; i++) {
        uint32_t tfd = prt_r32(base, port, PRT_TFD);
        if (!(tfd & (ATA_BSY | ATA_DRQ))) break;
        io_pause();
    }

    prt_w32(base, port, PRT_CI, 1u << slot);

    for (uint32_t i = 0; i < timeout_loops; i++) {
        uint32_t ci = prt_r32(base, port, PRT_CI);
        if ((ci & (1u << slot)) == 0) {
            uint32_t tfd = prt_r32(base, port, PRT_TFD);
            if (tfd & ATA_ERR) return -EIO;
            return 0;
        }
        if (prt_r32(base, port, PRT_IS) & (1u << 30)) {
            return -EIO;
        }
        for (int k = 0; k < 16; k++) io_pause();
    }
    return -ETIMEDOUT;
}

static void ata_fix_id_string(char *dst, const uint16_t *src, int words) {
    for (int i = 0; i < words; i++) {
        dst[i * 2 + 0] = (char)(src[i] >> 8);
        dst[i * 2 + 1] = (char)(src[i] & 0xFF);
    }
    dst[words * 2] = '\0';
    for (int i = words * 2 - 1; i >= 0 && dst[i] == ' '; i--) dst[i] = '\0';
}

static int ahci_identify(ahci_hba_t *hba, ahci_device_t *d) {
    volatile uint8_t *base = hba->abar;
    int port = d->port_idx;

    int slot = find_cmd_slot(base, port, hba->ncs);
    if (slot < 0) return -EBUSY;

    uint16_t *id = (uint16_t *)pmm_alloc_zero(1);
    if (!id) return -ENOMEM;
    uintptr_t id_phys = pmm_virt_to_phys(id);

    ahci_cmd_header_t *hdr = &((ahci_cmd_header_t *)d->clb_virt)[slot];
    hdr->flags = (uint16_t)(sizeof(ahci_fis_reg_h2d_t) / 4);
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    ahci_cmd_table_t *ct = (ahci_cmd_table_t *)((uint8_t *)d->ctba_virt + slot * 256);
    memset(ct, 0, sizeof(*ct));
    ct->prdt[0].dba  = (uint32_t)(id_phys & 0xFFFFFFFFu);
    ct->prdt[0].dbau = (uint32_t)(id_phys >> 32);
    ct->prdt[0].dbc  = 512 - 1;

    ahci_fis_reg_h2d_t *fis = (ahci_fis_reg_h2d_t *)ct->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pm       = 0x80;
    fis->command  = d->atapi ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;
    fis->device   = 0;

    int r = ahci_issue_cmd(hba, d, slot, 5000000);
    if (r != 0) {
        pmm_free(id, 1);
        return r;
    }

    ata_fix_id_string(d->serial,   &id[10], 10);
    ata_fix_id_string(d->firmware, &id[23], 4);
    ata_fix_id_string(d->model,    &id[27], 20);

    uint64_t sectors = 0;
    uint16_t cmd_set2 = id[83];
    int lba48 = (cmd_set2 & (1u << 10)) != 0;
    if (lba48) {
        sectors  =  (uint64_t)id[100];
        sectors |= ((uint64_t)id[101]) << 16;
        sectors |= ((uint64_t)id[102]) << 32;
        sectors |= ((uint64_t)id[103]) << 48;
    } else {
        sectors =  (uint64_t)id[60];
        sectors |= ((uint64_t)id[61]) << 16;
    }
    if (sectors == 0) sectors = 0;

    d->sectors    = sectors;
    d->size_bytes = sectors * AHCI_SECTOR_SIZE;

    pmm_free(id, 1);
    return 0;
}

#define AHCI_RW_CHUNK_SECTORS 128

static int ahci_rw(ahci_device_t *d, uint64_t lba, uint32_t count,
                   void *buf, int write) {
    if (!d || !d->present || !buf || count == 0) return -EINVAL;

    if (count > AHCI_RW_CHUNK_SECTORS) {
        uint8_t *bp = (uint8_t *)buf;
        while (count > 0) {
            uint32_t n = count > AHCI_RW_CHUNK_SECTORS ? AHCI_RW_CHUNK_SECTORS : count;
            int r = ahci_rw(d, lba, n, bp, write);
            if (r != 0) return r;
            lba += n;
            count -= n;
            bp += (size_t)n * AHCI_SECTOR_SIZE;
        }
        return 0;
    }

    spinlock_acquire(&g_ahci_lock);

    ahci_hba_t *hba = d->hba;
    volatile uint8_t *base = hba->abar;
    int port = d->port_idx;

    int slot = find_cmd_slot(base, port, hba->ncs);
    if (slot < 0) { spinlock_release(&g_ahci_lock); return -EBUSY; }

    uint32_t bytes = count * AHCI_SECTOR_SIZE;
    uint32_t pages = (bytes + 4095) / 4096;
    void *bounce = pmm_alloc(pages);
    if (!bounce) { spinlock_release(&g_ahci_lock); return -ENOMEM; }
    uintptr_t bounce_phys = pmm_virt_to_phys(bounce);

    if (write) memcpy(bounce, buf, bytes);

    ahci_cmd_header_t *hdr = &((ahci_cmd_header_t *)d->clb_virt)[slot];
    hdr->flags = (uint16_t)(sizeof(ahci_fis_reg_h2d_t) / 4);
    if (write) hdr->flags |= (1u << 6);
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    ahci_cmd_table_t *ct = (ahci_cmd_table_t *)((uint8_t *)d->ctba_virt + slot * 256);
    memset(ct, 0, sizeof(*ct));
    ct->prdt[0].dba  = (uint32_t)(bounce_phys & 0xFFFFFFFFu);
    ct->prdt[0].dbau = (uint32_t)(bounce_phys >> 32);
    ct->prdt[0].dbc  = bytes - 1;

    ahci_fis_reg_h2d_t *fis = (ahci_fis_reg_h2d_t *)ct->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pm       = 0x80;
    fis->command  = write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX;
    fis->lba0     = (uint8_t)(lba       & 0xFF);
    fis->lba1     = (uint8_t)((lba >>  8) & 0xFF);
    fis->lba2     = (uint8_t)((lba >> 16) & 0xFF);
    fis->device   = 0x40;
    fis->lba3     = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4     = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5     = (uint8_t)((lba >> 40) & 0xFF);
    fis->count    = (uint16_t)count;

    int r = ahci_issue_cmd(hba, d, slot, 10000000);
    if (r == 0 && !write) memcpy(buf, bounce, bytes);

    pmm_free(bounce, pages);
    spinlock_release(&g_ahci_lock);
    return r;
}

int ahci_read_sectors(ahci_device_t *d, uint64_t lba, uint32_t count, void *buf) {
    return ahci_rw(d, lba, count, buf, 0);
}
int ahci_write_sectors(ahci_device_t *d, uint64_t lba, uint32_t count, const void *buf) {
    return ahci_rw(d, lba, count, (void *)buf, 1);
}

int ahci_flush(ahci_device_t *d) {
    if (!d || !d->present) return -EINVAL;
    spinlock_acquire(&g_ahci_lock);
    ahci_hba_t *hba = d->hba;
    volatile uint8_t *base = hba->abar;
    int port = d->port_idx;

    int slot = find_cmd_slot(base, port, hba->ncs);
    if (slot < 0) { spinlock_release(&g_ahci_lock); return -EBUSY; }

    ahci_cmd_header_t *hdr = &((ahci_cmd_header_t *)d->clb_virt)[slot];
    hdr->flags = (uint16_t)(sizeof(ahci_fis_reg_h2d_t) / 4);
    hdr->prdtl = 0;
    hdr->prdbc = 0;

    ahci_cmd_table_t *ct = (ahci_cmd_table_t *)((uint8_t *)d->ctba_virt + slot * 256);
    memset(ct, 0, sizeof(*ct));
    ahci_fis_reg_h2d_t *fis = (ahci_fis_reg_h2d_t *)ct->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pm       = 0x80;
    fis->command  = ATA_CMD_FLUSH_CACHE_EX;

    int r = ahci_issue_cmd(hba, d, slot, 5000000);
    spinlock_release(&g_ahci_lock);
    return r;
}

static int ahci_probe(pci_device_t *pd) {
    if (g_hba_present) return 0;
    if (pd->class_code != 0x01 || pd->subclass != 0x06 || pd->prog_if != 0x01) return 0;

    pci_bar_t *bar5 = &pd->bars[5];
    if (bar5->type != PCI_BAR_TYPE_MEM || bar5->base == 0) {
        serial_writestring("[ahci] BAR5 not present\n");
        return -EINVAL;
    }

    uint16_t cmd = pci_config_read16(pd->segment, pd->bus, pd->device, pd->function,
                                     PCI_COMMAND);
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_config_write16(pd->segment, pd->bus, pd->device, pd->function, PCI_COMMAND, cmd);

    uint64_t hhdm = pmm_get_hhdm_offset();
    uint64_t bar_phys = bar5->base & ~0xFFFULL;
    uint64_t bar_size = (bar5->size + 0xFFF) & ~0xFFFULL;
    if (bar_size == 0) bar_size = 0x2000;
    size_t pages = (size_t)(bar_size >> 12);
    if (!paging_map_range(vmm_get_kernel_pagemap(),
                          (uintptr_t)(bar_phys + hhdm),
                          (uintptr_t)bar_phys,
                          pages,
                          VMM_PRESENT | VMM_WRITE | VMM_PCD)) {
        serial_printf("[ahci] failed to map ABAR at 0x%llx (%zu pages)\n",
                      (unsigned long long)bar_phys, pages);
        return -EIO;
    }

    volatile uint8_t *abar =
        (volatile uint8_t *)(uintptr_t)(bar5->base + hhdm);

    memset(&g_hba, 0, sizeof(g_hba));
    g_hba.abar = abar;
    g_hba.seg  = pd->segment; g_hba.bus = pd->bus;
    g_hba.dev  = pd->device;  g_hba.func = pd->function;

    uint32_t ghc = hba_r32(abar, HBA_GHC);
    ghc |= GHC_AE;
    ghc &= ~GHC_IE;
    hba_w32(abar, HBA_GHC, ghc);

    g_hba.cap  = hba_r32(abar, HBA_CAP);
    g_hba.pi   = hba_r32(abar, HBA_PI);
    g_hba.ncs  = ((g_hba.cap >> CAP_NCS_SHIFT) & CAP_NCS_MASK) + 1;
    g_hba.s64a = (g_hba.cap & CAP_S64A) != 0;

    serial_printf("[ahci] HBA at BAR5=0x%llx, cap=0x%x, pi=0x%x, slots=%u, s64a=%u\n",
                  (unsigned long long)bar5->base,
                  g_hba.cap, g_hba.pi, (unsigned)g_hba.ncs, (unsigned)g_hba.s64a);

    g_hba_present = 1;
    g_hba.port_count = 0;

    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(g_hba.pi & (1u << i))) continue;
        serial_printf("[ahci] probing port %d\n", i);

        uint32_t ssts = prt_r32(abar, i, PRT_SSTS);
        uint8_t  det  = ssts & SSTS_DET_MASK;
        if (det == 0 && (ssts & SSTS_IPM_MASK) == 0) {
            serial_printf("[ahci] port %d: no device wired (ssts=0)\n", i);
            continue;
        }
        for (int t = 0; t < 10000 && det != SSTS_DET_PRESENT; t++) {
            ssts = prt_r32(abar, i, PRT_SSTS);
            det  = ssts & SSTS_DET_MASK;
            if (det == SSTS_DET_PRESENT) break;
            for (int k = 0; k < 32; k++) io_pause();
        }
        if (det != SSTS_DET_PRESENT) {
            serial_printf("[ahci] port %d: link not up (ssts=0x%x)\n", i, ssts);
            continue;
        }

        for (int t = 0; t < 200000; t++) {
            uint32_t tfd = prt_r32(abar, i, PRT_TFD);
            if (!(tfd & ATA_BSY) && !(tfd & ATA_DRQ)) break;
            for (int k = 0; k < 64; k++) io_pause();
        }

        uint32_t sig = prt_r32(abar, i, PRT_SIG);
        ahci_device_t *d = &g_hba.ports[i];
        memset(d, 0, sizeof(*d));
        d->present  = true;
        d->port_idx = (uint8_t)i;
        d->hba      = &g_hba;
        d->atapi    = (sig == SIG_ATAPI);

        if (sig != SIG_SATA && sig != SIG_ATAPI) {
            serial_printf("[ahci] port %d: unknown signature 0x%x (ssts=0x%x), skipping\n",
                          i, sig, ssts);
            d->present = false;
            continue;
        }

        ahci_setup_port(&g_hba, i, d);

        int ir = ahci_identify(&g_hba, d);
        if (ir != 0) {
            serial_printf("[ahci] port %d: identify failed (%d)\n", i, ir);
            d->present = false;
            continue;
        }
        serial_printf("[ahci] port %d: %s '%s' sectors=%llu (%llu MB)\n",
                      i, d->atapi ? "ATAPI" : "SATA", d->model,
                      (unsigned long long)d->sectors,
                      (unsigned long long)(d->size_bytes / (1024 * 1024)));

        if (g_dev_count < AHCI_MAX_DEVICES) g_devs[g_dev_count++] = d;
        g_hba.port_count++;
    }
    serial_printf("[ahci] probe done, %d device(s) registered\n", g_dev_count);
    return 0;
}

static const pci_driver_t g_ahci_driver = {
    .name           = "ahci",
    .match_vendor   = -1,
    .match_device   = -1,
    .match_class    = 0x01,
    .match_subclass = 0x06,
    .probe          = ahci_probe,
};

void ahci_init(void) {
    pci_register_driver(&g_ahci_driver);
}

int ahci_device_count(void) { return g_dev_count; }
ahci_device_t *ahci_get_device(int index) {
    if (index < 0 || index >= g_dev_count) return NULL;
    return g_devs[index];
}
