#ifndef UDF_H
#define UDF_H

#include "vfs.h"
#include "../drivers/disk/blkdev.h"

int      udf_detect(blkdev_t *dev);
vnode_t *udf_mount(blkdev_t *dev);

#endif
