#ifndef RAMFS_H
#define RAMFS_H

#include "vfs.h"

#define RAMFS_MAX_CHILDREN   1024
#define RAMFS_CHUNK_SIZE     4096
#define RAMFS_MAX_CHUNKS     (16 * 1024 * 1024)

vnode_t *ramfs_create_root(void);

#endif