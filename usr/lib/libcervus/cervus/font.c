#include <sys/cervus.h>
#include <sys/syscall.h>
#include <stdint.h>

extern long __cervus_sys_ret(long r);

int cervus_setfont(unsigned w, unsigned h, unsigned nglyph,
                   const unsigned char *glyphs8, const unsigned short *cp2glyph) {
    return (int)__cervus_sys_ret((long)syscall5(SYS_SETFONT, w, h, nglyph, glyphs8, cp2glyph));
}

int cervus_setfont_reset(void) {
    return (int)__cervus_sys_ret((long)syscall5(SYS_SETFONT, 0, 0, 0, 0, 0));
}
