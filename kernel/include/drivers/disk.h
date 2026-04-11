#ifndef DISK_H
#define DISK_H

#include "../drivers/blkdev.h"

void disk_init(void);
int disk_mount(const char *devname, const char *path);
int disk_umount(const char *path);
int disk_format(const char *devname, const char *label);

#endif