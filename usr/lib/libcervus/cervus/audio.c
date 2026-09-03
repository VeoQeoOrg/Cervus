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

int cervus_audio_mixer_get(cervus_audio_mixer_t *m) {
    return (int)syscall3(SYS_AUDIO_MIXER, 0, 0, m);
}

int cervus_audio_set_volume(int pct) {
    return (int)syscall3(SYS_AUDIO_MIXER, 1, (long)pct, 0);
}

int cervus_audio_set_mute(int mute) {
    return (int)syscall3(SYS_AUDIO_MIXER, 2, (long)mute, 0);
}

int cervus_audio_set_output(int idx) {
    return (int)syscall3(SYS_AUDIO_MIXER, 3, (long)idx, 0);
}
