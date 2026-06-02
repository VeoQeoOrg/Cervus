#ifndef BLKDEV_H
#define BLKDEV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BLKDEV_MAX       64
#define BLKDEV_NAME_MAX  32
#define BLKDEV_SECTOR_SIZE 512

typedef struct blkdev blkdev_t;

typedef struct blkdev_ops {
    int (*read_sectors) (blkdev_t *dev, uint64_t lba, uint32_t count, void *buf);
    int (*write_sectors)(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf);
    int (*flush)        (blkdev_t *dev);
} blkdev_ops_t;

#define BLKDEV_MODEL_MAX 41

struct blkdev {
    char              name[BLKDEV_NAME_MAX];
    char              model[BLKDEV_MODEL_MAX];
    bool              present;
    bool              is_partition;
    uint64_t          sector_count;
    uint64_t          size_bytes;
    uint32_t          sector_size;
    const blkdev_ops_t *ops;
    void             *priv;
    uint64_t          part_lba_start;
    uint8_t           part_type;
    uint8_t           part_bootable;
};

int blkdev_register(blkdev_t *dev);
blkdev_t *blkdev_get_by_name(const char *name);
blkdev_t *blkdev_get(int index);
int blkdev_count(void);
void blkdev_init(void);
int blkdev_read(blkdev_t *dev, uint64_t offset, void *buf, size_t len);
int blkdev_write(blkdev_t *dev, uint64_t offset, const void *buf, size_t len);

#endif