#ifndef KERNEL_FS_ISO9660_H
#define KERNEL_FS_ISO9660_H

#include "../drivers/disk/blkdev.h"
#include "vfs.h"

#define ISO9660_SECTOR_SIZE      2048
#define ISO9660_PVD_LBA          16
#define ISO9660_ROOT_REC_OFFSET  156

vnode_t *iso9660_mount(blkdev_t *dev);

#endif
