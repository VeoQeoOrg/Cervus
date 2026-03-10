#ifndef INITRAMFS_H
#define INITRAMFS_H

#include <stddef.h>
#include <stdint.h>

int initramfs_mount(const void *data, size_t size);

#endif