#ifndef RAMFS_H
#define RAMFS_H

#include "vfs.h"

#define RAMFS_MAX_CHILDREN   64
#define RAMFS_MAX_FILE_SIZE  (4 * 1024 * 1024)

vnode_t *ramfs_create_root(void);

#endif