#include "../../../include/drivers/disk/ata.h"
#include "../../../include/drivers/pci.h"
#include "../../../include/io/ports.h"
#include "../../../include/io/serial.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/memory/dma.h"
#include "../../../include/apic/apic.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/syscall/errno.h"
#include <string.h>

static ata_drive_t g_drives[ATA_MAX_DRIVES];
static int         g_drive_count = 0;
static spinlock_t  g_ata_lock = SPINLOCK_INIT;

#define ATA_IO_TIMEOUT    20000000
#define ATA_WRITE_TIMEOUT 40000000
#define ATA_RETRY_COUNT   3

static void ata_io_wait(uint16_t ctrl) {
    inb(ctrl); inb(ctrl); inb(ctrl); inb(ctrl);
}

static inline void ata_cpu_relax(void) {
    asm volatile("pause" ::: "memory");
}

static void ata_soft_reset(uint16_t ctrl) {
    outb(ctrl, 0x04);
    ata_io_wait(ctrl);
    outb(ctrl, 0x00);
    ata_io_wait(ctrl);

    for (int i = 0; i < 2000000; i++) {
        uint8_t s = inb(ctrl);
        if (!(s & ATA_SR_BSY)) return;
        ata_io_wait(ctrl);
        ata_cpu_relax();
    }
}

static int ata_wait_ready(uint16_t io, uint16_t ctrl, int timeout_us) {
    (void)io;
    ata_io_wait(ctrl);
    for (int i = 0; i < timeout_us; i++) {
        uint8_t s = inb(ctrl + ATA_REG_ALT_STATUS);
        if (!(s & ATA_SR_BSY)) {
            if (s & ATA_SR_ERR) return -EIO;
            if (s & ATA_SR_DF)  return -EIO;
            return 0;
        }
        ata_io_wait(ctrl);
        ata_cpu_relax();
    }
    return -ETIMEDOUT;
}

static int ata_wait_drq(uint16_t io, uint16_t ctrl, int timeout_us) {
    (void)io;
    ata_io_wait(ctrl);
    for (int i = 0; i < timeout_us; i++) {
        uint8_t s = inb(ctrl + ATA_REG_ALT_STATUS);
        if (s & ATA_SR_ERR) return -EIO;
        if (s & ATA_SR_DF)  return -EIO;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
        ata_io_wait(ctrl);
        ata_cpu_relax();
    }
    return -ETIMEDOUT;
}

static void ata_fix_string(char *dst, const uint16_t *src, int words) {
    for (int i = 0; i < words; i++) {
        dst[i * 2 + 0] = (char)(src[i] >> 8);
        dst[i * 2 + 1] = (char)(src[i] & 0xFF);
    }
    dst[words * 2] = '\0';
    for (int i = words * 2 - 1; i >= 0 && dst[i] == ' '; i--)
        dst[i] = '\0';
}

static bool ata_identify_drive(uint16_t io, uint16_t ctrl, uint8_t drv_sel, ata_drive_t *out) {
    memset(out, 0, sizeof(*out));
    out->io_base      = io;
    out->ctrl_base     = ctrl;
    out->drive_select  = drv_sel;

    outb(io + ATA_REG_DRIVE, drv_sel);
    ata_io_wait(ctrl);

    outb(io + ATA_REG_SECCOUNT, 0);
    outb(io + ATA_REG_LBA_LO,  0);
    outb(io + ATA_REG_LBA_MID, 0);
    outb(io + ATA_REG_LBA_HI,  0);

    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_wait(ctrl);

    uint8_t status = inb(io + ATA_REG_STATUS);
    if (status == 0) return false;

    for (int i = 0; i < 1000000; i++) {
        status = inb(io + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) break;
        ata_cpu_relax();
    }
    if (status & ATA_SR_BSY) return false;

    uint8_t lm = inb(io + ATA_REG_LBA_MID);
    uint8_t lh = inb(io + ATA_REG_LBA_HI);
    if (lm == 0x14 && lh == 0xEB) {
        out->is_atapi = true;
        return false;
    }
    if (lm == 0x3C && lh == 0xC3) {

    }
    if (lm != 0 || lh != 0) {
        return false;
    }

    for (int i = 0; i < 1000000; i++) {
        status = inb(io + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return false;
        if (status & ATA_SR_DRQ) break;
        ata_cpu_relax();
    }
    if (!(status & ATA_SR_DRQ)) return false;

    for (int i = 0; i < 256; i++)
        out->identify[i] = inw(io + ATA_REG_DATA);

    ata_fix_string(out->model,    &out->identify[27], 20);
    ata_fix_string(out->serial,   &out->identify[10], 10);
    ata_fix_string(out->firmware,  &out->identify[23], 4);

    if (out->identify[83] & (1 << 10)) {
        out->lba48 = true;
        out->sectors = (uint64_t)out->identify[100]
                     | ((uint64_t)out->identify[101] << 16)
                     | ((uint64_t)out->identify[102] << 32)
                     | ((uint64_t)out->identify[103] << 48);
    } else {
        out->lba48 = false;
        out->sectors = (uint64_t)out->identify[60]
                     | ((uint64_t)out->identify[61] << 16);
    }
    out->size_bytes = out->sectors * ATA_SECTOR_SIZE;
    out->present    = true;
    return true;
}

static bool ata_channel_present(uint16_t io, uint16_t ctrl) {
    uint8_t s1 = inb(io + ATA_REG_STATUS);
    uint8_t s2 = inb(ctrl);
    if (s1 == 0xFF && s2 == 0xFF) return false;

    outb(io + ATA_REG_LBA_LO,  0x55);
    outb(io + ATA_REG_LBA_MID, 0xAA);
    uint8_t v1 = inb(io + ATA_REG_LBA_LO);
    uint8_t v2 = inb(io + ATA_REG_LBA_MID);
    if (v1 != 0x55 || v2 != 0xAA) return false;
    return true;
}

#define BMR_CMD        0x00
#define BMR_STATUS     0x02
#define BMR_PRDT_ADDR  0x04

#define BMR_CMD_START  0x01
#define BMR_CMD_READ   0x08

#define BMR_STS_IRQ    0x04
#define BMR_STS_ERR    0x02
#define BMR_STS_ACTIVE 0x01

#define ATA_DMA_BUF_SIZE  (64 * 1024)

typedef struct __attribute__((packed)) {
    uint32_t phys;
    uint16_t count;
    uint16_t flags;
} prdt_entry_t;

static uint16_t g_bmr_primary   = 0;
static uint16_t g_bmr_secondary = 0;

static void ata_pci_find_bmr(void) {
    int n = pci_device_count();
    for (int i = 0; i < n; i++) {
        pci_device_t *pd = pci_device_at(i);
        if (!pd) continue;
        if (pd->class_code != 0x01) continue;
        if (pd->subclass != 0x01 && pd->subclass != 0x06) continue;

        pci_bar_t *b4 = &pd->bars[4];
        if (b4->type != PCI_BAR_TYPE_IO || b4->base == 0) continue;

        uint16_t cmd = pci_config_read16(pd->segment, pd->bus, pd->device,
                                         pd->function, PCI_COMMAND);
        cmd |= PCI_COMMAND_MASTER | PCI_COMMAND_IO;
        pci_config_write16(pd->segment, pd->bus, pd->device, pd->function,
                           PCI_COMMAND, cmd);

        g_bmr_primary   = (uint16_t)b4->base;
        g_bmr_secondary = (uint16_t)(b4->base + 8);
        serial_printf("[ATA] PCI %02x:%02x.%u IDE BMR=0x%04x (class=%u:%u prog_if=0x%02x)\n",
                      pd->bus, pd->device, pd->function,
                      g_bmr_primary, pd->class_code, pd->subclass, pd->prog_if);
        return;
    }
    serial_writestring("[ATA] no PCI IDE/SATA bus-master found (PIO only)\n");
}

static int ata_setup_dma(ata_drive_t *drv) {
    drv->dma_supported = false;
    if (drv->is_atapi) return 0;

    uint16_t bmr = (drv->io_base == ATA_PRIMARY_IO) ? g_bmr_primary : g_bmr_secondary;
    if (bmr == 0) return 0;

    uintptr_t prdt_phys;
    void *prdt = dma_alloc_coherent(4096, &prdt_phys);
    if (!prdt) return -ENOMEM;
    if (prdt_phys >= 0xFFFFFFFFULL) {
        serial_printf("[ATA] PRDT phys 0x%llx >4GB, DMA disabled\n",
                      (unsigned long long)prdt_phys);
        return 0;
    }

    uintptr_t buf_phys;
    void *buf = dma_alloc_coherent(ATA_DMA_BUF_SIZE, &buf_phys);
    if (!buf) return -ENOMEM;
    if (buf_phys + ATA_DMA_BUF_SIZE > 0xFFFFFFFFULL) {
        serial_printf("[ATA] DMA buf phys 0x%llx >4GB, DMA disabled\n",
                      (unsigned long long)buf_phys);
        return 0;
    }

    drv->bmr_base     = bmr;
    drv->prdt_phys    = prdt_phys;
    drv->prdt_virt    = prdt;
    drv->dma_buf_phys = buf_phys;
    drv->dma_buf_virt = buf;
    drv->dma_buf_size = ATA_DMA_BUF_SIZE;
    drv->dma_supported = true;

    serial_printf("[ATA] drive %u: DMA enabled (BMR=0x%04x, PRDT@0x%llx, buf@0x%llx)\n",
                  drv->drive_select == ATA_DRIVE_MASTER ? 0 : 1,
                  bmr,
                  (unsigned long long)prdt_phys,
                  (unsigned long long)buf_phys);
    return 0;
}

static int ata_dma_xfer_once(ata_drive_t *drv, uint64_t lba, uint32_t count,
                             void *user_buf, bool is_write)
{
    if (!drv->dma_supported) return -ENOSYS;
    if (count == 0 || count * 512U > drv->dma_buf_size) return -EINVAL;

    uint16_t io   = drv->io_base;
    uint16_t ctrl = drv->ctrl_base;
    uint16_t bmr  = drv->bmr_base;
    uint32_t bytes = count * 512U;

    if (is_write) memcpy(drv->dma_buf_virt, user_buf, bytes);

    prdt_entry_t *prdt = (prdt_entry_t *)drv->prdt_virt;
    prdt[0].phys  = (uint32_t)drv->dma_buf_phys;
    prdt[0].count = (uint16_t)(bytes == 0x10000 ? 0 : bytes);
    prdt[0].flags = 0x8000;

    outb(bmr + BMR_CMD, 0);
    asm volatile("" ::: "memory");

    uint32_t prdt_lo = (uint32_t)drv->prdt_phys;
    outb(bmr + BMR_PRDT_ADDR + 0, (uint8_t)(prdt_lo >> 0));
    outb(bmr + BMR_PRDT_ADDR + 1, (uint8_t)(prdt_lo >> 8));
    outb(bmr + BMR_PRDT_ADDR + 2, (uint8_t)(prdt_lo >> 16));
    outb(bmr + BMR_PRDT_ADDR + 3, (uint8_t)(prdt_lo >> 24));

    outb(bmr + BMR_STATUS, BMR_STS_IRQ | BMR_STS_ERR);

    outb(io + ATA_REG_DRIVE, drv->drive_select);
    ata_io_wait(ctrl);
    if (ata_wait_ready(io, ctrl, ATA_IO_TIMEOUT) < 0) return -EIO;

    if (drv->lba48 && (lba > 0x0FFFFFFF || count > 256)) {
        outb(io + ATA_REG_DRIVE, (drv->drive_select & 0xF0) | ATA_LBA_BIT);
        outb(io + ATA_REG_SECCOUNT, (uint8_t)(count >> 8));
        outb(io + ATA_REG_LBA_LO,  (uint8_t)(lba >> 24));
        outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 32));
        outb(io + ATA_REG_LBA_HI,  (uint8_t)(lba >> 40));
        outb(io + ATA_REG_SECCOUNT, (uint8_t)count);
        outb(io + ATA_REG_LBA_LO,  (uint8_t)lba);
        outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
        outb(io + ATA_REG_LBA_HI,  (uint8_t)(lba >> 16));
        outb(io + ATA_REG_COMMAND,
             is_write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT);
    } else {
        outb(io + ATA_REG_DRIVE,
             (drv->drive_select & 0xF0) | ATA_LBA_BIT | ((lba >> 24) & 0xF));
        outb(io + ATA_REG_SECCOUNT, (uint8_t)count);
        outb(io + ATA_REG_LBA_LO,  (uint8_t)lba);
        outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
        outb(io + ATA_REG_LBA_HI,  (uint8_t)(lba >> 16));
        outb(io + ATA_REG_COMMAND,
             is_write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA);
    }
    ata_io_wait(ctrl);

    uint8_t cmd_byte = is_write ? 0 : BMR_CMD_READ;
    outb(bmr + BMR_CMD, cmd_byte | BMR_CMD_START);

    uint64_t deadline = hpet_elapsed_ns() + 30000000000ULL;
    for (;;) {
        uint8_t st = inb(bmr + BMR_STATUS);
        uint8_t sr = inb(ctrl + ATA_REG_ALT_STATUS);
        if (st & BMR_STS_ERR) {
            outb(bmr + BMR_CMD, 0);
            outb(bmr + BMR_STATUS, BMR_STS_IRQ | BMR_STS_ERR);
            return -EIO;
        }
        if (!(st & BMR_STS_ACTIVE) && (st & BMR_STS_IRQ) && !(sr & ATA_SR_BSY))
            break;
        if (hpet_elapsed_ns() > deadline) {
            outb(bmr + BMR_CMD, 0);
            return -ETIMEDOUT;
        }
        ata_cpu_relax();
    }

    outb(bmr + BMR_CMD, 0);
    outb(bmr + BMR_STATUS, BMR_STS_IRQ | BMR_STS_ERR);

    uint8_t final_sr = inb(io + ATA_REG_STATUS);
    if (final_sr & (ATA_SR_ERR | ATA_SR_DF)) return -EIO;

    if (!is_write) memcpy(user_buf, drv->dma_buf_virt, bytes);
    return 0;
}

void ata_init(void) {
    serial_writestring("[ATA] probing drives...\n");
    g_drive_count = 0;

    ata_pci_find_bmr();

    struct { uint16_t io; uint16_t ctrl; uint8_t irq; } channels[] = {
        { ATA_PRIMARY_IO,   ATA_PRIMARY_CTRL,   14 },
        { ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, 15 },
    };

    for (int ch = 0; ch < 2; ch++) {
        if (!ata_channel_present(channels[ch].io, channels[ch].ctrl)) {
            serial_printf("[ATA] channel %d not present, skipping\n", ch);
            continue;
        }

        outb(channels[ch].ctrl, 0x02);
        ata_soft_reset(channels[ch].ctrl);

        uint8_t drvs[] = { ATA_DRIVE_MASTER, ATA_DRIVE_SLAVE };
        for (int d = 0; d < 2; d++) {
            int idx = ch * 2 + d;
            ata_drive_t *drv = &g_drives[idx];
            if (ata_identify_drive(channels[ch].io, channels[ch].ctrl,
                                   drvs[d], drv))
            {
                drv->irq = channels[ch].irq;
                ata_setup_dma(drv);
                g_drive_count++;
                serial_printf("[ATA] drive %d: '%s'  %llu sectors (%llu MB) %s%s\n",
                    idx, drv->model, drv->sectors,
                    drv->size_bytes / (1024 * 1024),
                    drv->lba48 ? "LBA48" : "LBA28",
                    drv->dma_supported ? " +DMA" : "");
            }
        }
    }

    if (g_drive_count == 0)
        serial_writestring("[ATA] no drives found\n");
    else
        serial_printf("[ATA] %d drive(s) detected\n", g_drive_count);
}

ata_drive_t *ata_get_drive(int index) {
    if (index < 0 || index >= ATA_MAX_DRIVES) return NULL;
    if (!g_drives[index].present) return NULL;
    return &g_drives[index];
}

int ata_get_drive_count(void) {
    return g_drive_count;
}


static int ata_read_sectors_once(ata_drive_t *drive, uint64_t lba,
                                 uint32_t count, void *buffer)
{
    uint16_t io   = drive->io_base;
    uint16_t ctrl = drive->ctrl_base;
    uint16_t *buf = (uint16_t *)buffer;
    int ret = 0;

    outb(io + ATA_REG_DRIVE, drive->drive_select);
    ata_io_wait(ctrl);
    (void)inb(io + ATA_REG_STATUS);
    ret = ata_wait_ready(io, ctrl, ATA_IO_TIMEOUT);
    if (ret < 0) return ret;

    if (drive->lba48 && (lba > 0x0FFFFFFF || count > 256)) {
        outb(io + ATA_REG_DRIVE, (drive->drive_select & 0xF0) | ATA_LBA_BIT);
        ata_io_wait(ctrl);

        outb(io + ATA_REG_SECCOUNT, (uint8_t)(count >> 8));
        outb(io + ATA_REG_LBA_LO,  (uint8_t)(lba >> 24));
        outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 32));
        outb(io + ATA_REG_LBA_HI,  (uint8_t)(lba >> 40));
        outb(io + ATA_REG_SECCOUNT, (uint8_t)(count));
        outb(io + ATA_REG_LBA_LO,  (uint8_t)(lba));
        outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
        outb(io + ATA_REG_LBA_HI,  (uint8_t)(lba >> 16));

        outb(io + ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);
    } else {
        outb(io + ATA_REG_DRIVE,
             (drive->drive_select & 0xF0) | ATA_LBA_BIT | ((lba >> 24) & 0x0F));
        ata_io_wait(ctrl);
        outb(io + ATA_REG_SECCOUNT, (uint8_t)count);
        outb(io + ATA_REG_LBA_LO,  (uint8_t)(lba));
        outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
        outb(io + ATA_REG_LBA_HI,  (uint8_t)(lba >> 16));

        outb(io + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    }

    ata_io_wait(ctrl);

    for (uint32_t s = 0; s < count; s++) {
        ret = ata_wait_drq(io, ctrl, ATA_IO_TIMEOUT);
        if (ret < 0) return ret;

        for (int i = 0; i < 256; i++)
            *buf++ = inw(io + ATA_REG_DATA);

        ata_io_wait(ctrl);
    }
    return 0;
}

int ata_read_sectors(ata_drive_t *drive, uint64_t lba,
                     uint32_t count, void *buffer)
{
    if (!drive || !drive->present || !buffer) return -EINVAL;
    if (count == 0) return 0;
    if (lba + count > drive->sectors) return -EINVAL;

    uint64_t flags = spinlock_acquire_irqsave(&g_ata_lock);
    int ret = -EIO;

    if (drive->dma_supported) {
        uint32_t chunk = drive->dma_buf_size / 512;
        uint32_t done = 0;
        ret = 0;
        while (done < count) {
            uint32_t take = count - done;
            if (take > chunk) take = chunk;
            ret = ata_dma_xfer_once(drive, lba + done, take,
                                    (uint8_t *)buffer + (size_t)done * 512, false);
            if (ret < 0) {
                serial_printf("[ATA] DMA read lba=%llu count=%u failed: %d (falling back to PIO)\n",
                              lba + done, take, ret);
                ata_soft_reset(drive->ctrl_base);
                drive->dma_supported = false;
                break;
            }
            done += take;
        }
        if (ret == 0 && drive->dma_supported) {
            spinlock_release_irqrestore(&g_ata_lock, flags);
            return 0;
        }
    }

    for (int attempt = 0; attempt < ATA_RETRY_COUNT; attempt++) {
        ret = ata_read_sectors_once(drive, lba, count, buffer);
        if (ret == 0) break;
        serial_printf("[ATA] read lba=%llu count=%u attempt %d failed: %d\n",
                      lba, count, attempt + 1, ret);
        ata_soft_reset(drive->ctrl_base);
        for (volatile int k = 0; k < 100000; k++) ata_cpu_relax();
    }

    spinlock_release_irqrestore(&g_ata_lock, flags);
    return ret;
}

static int ata_write_sectors_once(ata_drive_t *drive, uint64_t lba,
                                  uint32_t count, const void *buffer)
{
    uint16_t io   = drive->io_base;
    uint16_t ctrl = drive->ctrl_base;
    const uint16_t *buf = (const uint16_t *)buffer;
    int ret = 0;

    outb(io + ATA_REG_DRIVE, drive->drive_select);
    ata_io_wait(ctrl);
    (void)inb(io + ATA_REG_STATUS);
    ret = ata_wait_ready(io, ctrl, ATA_IO_TIMEOUT);
    if (ret < 0) return ret;

    if (drive->lba48 && (lba > 0x0FFFFFFF || count > 256)) {
        outb(io + ATA_REG_DRIVE, (drive->drive_select & 0xF0) | ATA_LBA_BIT);
        ata_io_wait(ctrl);

        outb(io + ATA_REG_SECCOUNT, (uint8_t)(count >> 8));
        outb(io + ATA_REG_LBA_LO,  (uint8_t)(lba >> 24));
        outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 32));
        outb(io + ATA_REG_LBA_HI,  (uint8_t)(lba >> 40));

        outb(io + ATA_REG_SECCOUNT, (uint8_t)(count));
        outb(io + ATA_REG_LBA_LO,  (uint8_t)(lba));
        outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
        outb(io + ATA_REG_LBA_HI,  (uint8_t)(lba >> 16));

        outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO_EXT);
    } else {
        outb(io + ATA_REG_DRIVE,
             (drive->drive_select & 0xF0) | ATA_LBA_BIT | ((lba >> 24) & 0x0F));
        ata_io_wait(ctrl);
        outb(io + ATA_REG_SECCOUNT, (uint8_t)count);
        outb(io + ATA_REG_LBA_LO,  (uint8_t)(lba));
        outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
        outb(io + ATA_REG_LBA_HI,  (uint8_t)(lba >> 16));

        outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    }

    ata_io_wait(ctrl);

    for (uint32_t s = 0; s < count; s++) {
        ret = ata_wait_drq(io, ctrl, ATA_WRITE_TIMEOUT);
        if (ret < 0) return ret;

        for (int i = 0; i < 256; i++)
            outw(io + ATA_REG_DATA, *buf++);

        ata_io_wait(ctrl);
    }

    ret = ata_wait_ready(io, ctrl, ATA_IO_TIMEOUT);
    if (ret < 0) return ret;

    return 0;
}

int ata_write_sectors(ata_drive_t *drive, uint64_t lba,
                      uint32_t count, const void *buffer)
{
    if (!drive || !drive->present || !buffer) return -EINVAL;
    if (count == 0) return 0;
    if (lba + count > drive->sectors) return -EINVAL;

    uint64_t flags = spinlock_acquire_irqsave(&g_ata_lock);
    int ret = -EIO;

    if (drive->dma_supported) {
        uint32_t chunk = drive->dma_buf_size / 512;
        uint32_t done = 0;
        ret = 0;
        while (done < count) {
            uint32_t take = count - done;
            if (take > chunk) take = chunk;
            ret = ata_dma_xfer_once(drive, lba + done, take,
                                    (void *)((const uint8_t *)buffer +
                                             (size_t)done * 512), true);
            if (ret < 0) {
                serial_printf("[ATA] DMA write lba=%llu count=%u failed: %d (falling back to PIO)\n",
                              lba + done, take, ret);
                ata_soft_reset(drive->ctrl_base);
                drive->dma_supported = false;
                break;
            }
            done += take;
        }
        if (ret == 0 && drive->dma_supported) {
            spinlock_release_irqrestore(&g_ata_lock, flags);
            return 0;
        }
    }

    for (int attempt = 0; attempt < ATA_RETRY_COUNT; attempt++) {
        ret = ata_write_sectors_once(drive, lba, count, buffer);
        if (ret == 0) break;
        serial_printf("[ATA] write lba=%llu count=%u attempt %d failed: %d\n",
                      lba, count, attempt + 1, ret);
        ata_soft_reset(drive->ctrl_base);
        for (volatile int k = 0; k < 100000; k++) ata_cpu_relax();
    }

    spinlock_release_irqrestore(&g_ata_lock, flags);
    return ret;
}

int ata_flush(ata_drive_t *drive) {
    if (!drive || !drive->present) return -EINVAL;

    uint64_t flags = spinlock_acquire_irqsave(&g_ata_lock);

    uint16_t io   = drive->io_base;
    uint16_t ctrl = drive->ctrl_base;

    outb(io + ATA_REG_DRIVE, drive->drive_select);
    outb(io + ATA_REG_COMMAND,
         drive->lba48 ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);

    int ret = ata_wait_ready(io, ctrl, ATA_IO_TIMEOUT);

    spinlock_release_irqrestore(&g_ata_lock, flags);
    return ret;
}