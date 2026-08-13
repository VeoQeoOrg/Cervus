#include "../../include/syscall/syscall_internal.h"
#include "../../include/drivers/audio/ac97.h"

int64_t sys_audio_open(uint64_t rate) {
    int r = ac97_open((uint32_t)rate);
    return r < 0 ? -ENODEV : 0;
}

int64_t sys_audio_write(uint64_t buf_ptr, uint64_t len) {
    if (!buf_ptr || !len) return 0;
    if (len > (1u << 20)) return -EINVAL;
    if (!syscall_uptr_validate((void *)buf_ptr, (size_t)len)) return -EFAULT;
    long r = ac97_write((const void *)buf_ptr, (size_t)len);
    return r < 0 ? -ENODEV : (int64_t)r;
}

int64_t sys_audio_close(void) {
    return ac97_close() < 0 ? -ENODEV : 0;
}
