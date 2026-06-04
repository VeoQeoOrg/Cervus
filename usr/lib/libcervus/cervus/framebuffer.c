#include <sys/cervus.h>
#include <sys/syscall.h>
#include <stdint.h>

int cervus_fb_info(cervus_fb_info_t *out) {
    return (int)syscall1(SYS_FB_INFO, out);
}

long cervus_fb_blit(const void *buf, unsigned x, unsigned y, unsigned w, unsigned h) {
    return (long)syscall5(SYS_FB_BLIT, buf, x, y, w, h);
}

void *cervus_fb_map(void) {
    uint64_t addr = 0;
    long r = (long)syscall1(SYS_FB_MAP, &addr);
    if (r < 0) return (void *)0;
    return (void *)(uintptr_t)addr;
}

int cervus_fb_acquire(void) { return (int)syscall0(SYS_FB_ACQUIRE); }
int cervus_fb_release(void) { return (int)syscall0(SYS_FB_RELEASE); }
