#include "../../include/syscall/syscall_internal.h"
#include "../../include/drivers/audio/audio.h"
#include <string.h>

int64_t sys_audio_open(uint64_t rate) {
    int r = audio_open((uint32_t)rate);
    return r < 0 ? -ENODEV : 0;
}

int64_t sys_audio_write(uint64_t buf_ptr, uint64_t len) {
    if (!buf_ptr || !len) return 0;
    if (len > (1u << 20)) return -EINVAL;
    if (!syscall_uptr_validate((void *)buf_ptr, (size_t)len)) return -EFAULT;
    long r = audio_write((const void *)buf_ptr, (size_t)len);
    return r < 0 ? -ENODEV : (int64_t)r;
}

int64_t sys_audio_close(void) {
    return audio_close() < 0 ? -ENODEV : 0;
}

#define AUDIO_MIXER_GET        0
#define AUDIO_MIXER_SET_VOL    1
#define AUDIO_MIXER_SET_MUTE   2
#define AUDIO_MIXER_SET_OUTPUT 3

int64_t sys_audio_mixer(uint64_t op, uint64_t arg, uint64_t uptr) {
    switch (op) {
        case AUDIO_MIXER_GET: {
            if (!uptr) return -EINVAL;
            if (!syscall_uptr_validate((void *)uptr, sizeof(audio_mixer_t))) return -EFAULT;
            audio_mixer_t m;
            if (audio_mixer_get(&m) != 0) return -ENODEV;
            memcpy((void *)uptr, &m, sizeof m);
            return 0;
        }
        case AUDIO_MIXER_SET_VOL:
            return audio_set_volume((int)(int64_t)arg) < 0 ? -ENODEV : 0;
        case AUDIO_MIXER_SET_MUTE:
            return audio_set_mute((int)(int64_t)arg) < 0 ? -ENODEV : 0;
        case AUDIO_MIXER_SET_OUTPUT:
            return audio_set_output((int)(int64_t)arg) < 0 ? -EINVAL : 0;
        default:
            return -EINVAL;
    }
}
