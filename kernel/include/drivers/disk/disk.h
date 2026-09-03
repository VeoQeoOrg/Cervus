#ifndef DISK_H
#define DISK_H

#include "blkdev.h"

void disk_init(void);
void disk_start_media_worker(void);
int disk_mount(const char *devname, const char *path);
int disk_umount(const char *path);
int disk_format(const char *devname, const char *label, int ext4);

void fmt_progress_begin(void);
void fmt_progress_set(int pct);
void fmt_progress_end(void);
int  fmt_progress_get(void);

#endif