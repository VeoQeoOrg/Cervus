#include <sys/cervus.h>
#include <sys/syscall.h>
#include <stddef.h>

int cervus_audio_open(unsigned rate) {
    return (int)syscall1(SYS_AUDIO_OPEN, rate);
}

long cervus_audio_write(const void *pcm, unsigned long len) {
    return (long)syscall2(SYS_AUDIO_WRITE, pcm, len);
}

int cervus_audio_close(void) {
    return (int)syscall0(SYS_AUDIO_CLOSE);
}
