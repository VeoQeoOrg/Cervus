#include "../../include/drivers/blkdev.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/syscall/errno.h"
#include <string.h>

static blkdev_t *g_blkdevs[BLKDEV_MAX];
static int        g_blkdev_count = 0;

void blkdev_init(void) {
    memset(g_blkdevs, 0, sizeof(g_blkdevs));
    g_blkdev_count = 0;
    serial_writestring("[blkdev] initialized\n");
}

int blkdev_register(blkdev_t *dev) {
    if (!dev || g_blkdev_count >= BLKDEV_MAX) return -ENOMEM;
    int idx = g_blkdev_count;
    g_blkdevs[idx] = dev;
    g_blkdev_count++;
    serial_printf("[blkdev] registered '%s' (%llu sectors, %llu MB)\n",
                  dev->name, dev->sector_count,
                  dev->size_bytes / (1024 * 1024));
    return idx;
}

blkdev_t *blkdev_get_by_name(const char *name) {
    for (int i = 0; i < g_blkdev_count; i++) {
        if (g_blkdevs[i] && strcmp(g_blkdevs[i]->name, name) == 0)
            return g_blkdevs[i];
    }
    return NULL;
}

blkdev_t *blkdev_get(int index) {
    if (index < 0 || index >= g_blkdev_count) return NULL;
    return g_blkdevs[index];
}

int blkdev_count(void) {
    return g_blkdev_count;
}

int blkdev_read(blkdev_t *dev, uint64_t offset, void *buf, size_t len) {
    if (!dev || !dev->ops || !dev->ops->read_sectors) return -EIO;
    if (len == 0) return 0;

    uint32_t sec_size = dev->sector_size ? dev->sector_size : BLKDEV_SECTOR_SIZE;
    uint64_t start_lba = offset / sec_size;
    uint64_t end_byte  = offset + len;
    uint64_t end_lba   = (end_byte + sec_size - 1) / sec_size;
    uint32_t nsectors  = (uint32_t)(end_lba - start_lba);

    uint8_t *tmp = kmalloc((size_t)nsectors * sec_size);
    if (!tmp) return -ENOMEM;

    int ret = dev->ops->read_sectors(dev, start_lba, nsectors, tmp);
    if (ret < 0) {
        kfree(tmp);
        return ret;
    }

    uint64_t off_in_buf = offset - start_lba * sec_size;
    memcpy(buf, tmp + off_in_buf, len);
    kfree(tmp);
    return 0;
}

int blkdev_write(blkdev_t *dev, uint64_t offset, const void *buf, size_t len) {
    if (!dev || !dev->ops || !dev->ops->write_sectors) return -EIO;
    if (len == 0) return 0;

    uint32_t sec_size = dev->sector_size ? dev->sector_size : BLKDEV_SECTOR_SIZE;
    uint64_t start_lba = offset / sec_size;
    uint64_t end_byte  = offset + len;
    uint64_t end_lba   = (end_byte + sec_size - 1) / sec_size;
    uint32_t nsectors  = (uint32_t)(end_lba - start_lba);

    uint8_t *tmp = kmalloc((size_t)nsectors * sec_size);
    if (!tmp) return -ENOMEM;

    int ret = dev->ops->read_sectors(dev, start_lba, nsectors, tmp);
    if (ret < 0) {
        kfree(tmp);
        return ret;
    }

    uint64_t off_in_buf = offset - start_lba * sec_size;
    memcpy(tmp + off_in_buf, buf, len);

    ret = dev->ops->write_sectors(dev, start_lba, nsectors, tmp);
    kfree(tmp);

    return ret;
}