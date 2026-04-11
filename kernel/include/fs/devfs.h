#ifndef DEVFS_H
#define DEVFS_H

#include "vfs.h"

vnode_t *devfs_create_root(void);
void devfs_register(const char *name, vnode_t *node);

#endif