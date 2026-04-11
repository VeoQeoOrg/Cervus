#ifndef BLKDEV_H
#define BLKDEV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BLKDEV_MAX       8
#define BLKDEV_NAME_MAX  32
#define BLKDEV_SECTOR_SIZE 512

typedef struct blkdev blkdev_t;

typedef struct blkdev_ops {
    int (*read_sectors) (blkdev_t *dev, uint64_t lba, uint32_t count, void *buf);
    int (*write_sectors)(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf);
    int (*flush)        (blkdev_t *dev);
} blkdev_ops_t;

struct blkdev {
    char              name[BLKDEV_NAME_MAX];
    bool              present;
    uint64_t          sector_count;
    uint64_t          size_bytes;
    uint32_t          sector_size;
    const blkdev_ops_t *ops;
    void             *priv;
};

int blkdev_register(blkdev_t *dev);
blkdev_t *blkdev_get_by_name(const char *name);
blkdev_t *blkdev_get(int index);
int blkdev_count(void);
void blkdev_init(void);
int blkdev_read(blkdev_t *dev, uint64_t offset, void *buf, size_t len);
int blkdev_write(blkdev_t *dev, uint64_t offset, const void *buf, size_t len);

#endif