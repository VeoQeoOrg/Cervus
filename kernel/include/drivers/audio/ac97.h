#ifndef KERNEL_DRIVERS_AUDIO_AC97_H
#define KERNEL_DRIVERS_AUDIO_AC97_H

#include <stdint.h>
#include <stddef.h>

void ac97_init(void);

int  ac97_open(uint32_t rate);
long ac97_write(const void *pcm, size_t bytes);
int  ac97_close(void);

#endif
