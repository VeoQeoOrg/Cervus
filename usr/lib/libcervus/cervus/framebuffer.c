#include <sys/cervus.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/mman_shared.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

extern char *program_invocation_short_name;

int cervus_display_info(cervus_fb_info_t *out) {
    return (int)syscall1(SYS_FB_INFO, out);
}

enum {
    O_FREE, O_DISPLAY, O_REGISTRY, O_SYNC, O_FRAME, O_COMPOSITOR, O_SHM, O_POOL,
    O_BUFFER, O_SURFACE, O_WM, O_XSURF, O_TOPLEVEL, O_SEAT, O_KEYBOARD, O_POINTER,
    O_RELMGR, O_REL, O_CONSTRAINTS, O_LOCKED, O_ZOMBIE
};

#define DECO_H    26

static const uint8_t deco_font[95 * 16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x24, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x24, 0x24, 0x24, 0x7e, 0x24, 0x24, 0x7e, 0x24, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x10, 0x7c, 0x92, 0x90, 0x90, 0x7c, 0x12, 0x12, 0x92, 0x7c, 0x10, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x94, 0x68, 0x08, 0x10, 0x10, 0x20, 0x2c, 0x52, 0x4c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x24, 0x24, 0x18, 0x30, 0x4a, 0x44, 0x44, 0x44, 0x3a, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x10, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x20, 0x10, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x10, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x18, 0x7e, 0x18, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x7c, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x04, 0x08, 0x08, 0x10, 0x10, 0x20, 0x20, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x46, 0x4a, 0x52, 0x62, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x18, 0x28, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x3e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x7e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x02, 0x1c, 0x02, 0x02, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x06, 0x0a, 0x12, 0x22, 0x42, 0x7e, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7e, 0x40, 0x40, 0x40, 0x7c, 0x02, 0x02, 0x02, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x1c, 0x20, 0x40, 0x40, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7e, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x3e, 0x02, 0x02, 0x04, 0x38, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x10, 0x10, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x04, 0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x04, 0x08, 0x08, 0x00, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7c, 0x82, 0x9e, 0xa2, 0xa2, 0xa2, 0xa6, 0x9a, 0x80, 0x7e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7c, 0x42, 0x42, 0x42, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x7c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x40, 0x40, 0x40, 0x40, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x78, 0x44, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x44, 0x78, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7e, 0x40, 0x40, 0x40, 0x78, 0x40, 0x40, 0x40, 0x40, 0x7e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7e, 0x40, 0x40, 0x40, 0x78, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x40, 0x40, 0x4e, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x38, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x42, 0x44, 0x48, 0x50, 0x60, 0x60, 0x50, 0x48, 0x44, 0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x82, 0xc6, 0xaa, 0x92, 0x92, 0x82, 0x82, 0x82, 0x82, 0x82, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x42, 0x42, 0x42, 0x62, 0x52, 0x4a, 0x46, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x7c, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x4a, 0x3c, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x7c, 0x50, 0x48, 0x44, 0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x42, 0x40, 0x40, 0x3c, 0x02, 0x02, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xfe, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x24, 0x24, 0x24, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x82, 0x82, 0x82, 0x82, 0x82, 0x92, 0x92, 0xaa, 0xc6, 0x82, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x42, 0x42, 0x24, 0x24, 0x18, 0x18, 0x24, 0x24, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x82, 0x82, 0x44, 0x44, 0x28, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7e, 0x02, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x40, 0x7e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x38, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x38, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x40, 0x20, 0x20, 0x10, 0x10, 0x08, 0x08, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x38, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x38, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00,
    0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x02, 0x3e, 0x42, 0x42, 0x42, 0x3e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x40, 0x40, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x40, 0x40, 0x40, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x02, 0x02, 0x3e, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x7e, 0x40, 0x40, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0e, 0x10, 0x10, 0x7c, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3e, 0x02, 0x02, 0x3c, 0x00,
    0x00, 0x00, 0x40, 0x40, 0x40, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x10, 0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x44, 0x44, 0x38, 0x00,
    0x00, 0x00, 0x40, 0x40, 0x40, 0x42, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7c, 0x40, 0x40, 0x40, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3e, 0x02, 0x02, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x5e, 0x60, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x40, 0x40, 0x3c, 0x02, 0x02, 0x7c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x10, 0x10, 0x7c, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x24, 0x24, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x82, 0x82, 0x92, 0x92, 0x92, 0x92, 0x7c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x24, 0x18, 0x24, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3e, 0x02, 0x02, 0x3c, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x04, 0x08, 0x10, 0x20, 0x40, 0x7e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0c, 0x10, 0x10, 0x10, 0x20, 0x10, 0x10, 0x10, 0x10, 0x0c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x30, 0x08, 0x08, 0x08, 0x04, 0x08, 0x08, 0x08, 0x08, 0x30, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x62, 0x92, 0x8c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

#define WL_OBJS   256
#define WL_EVQ    256
#define WL_IN     16384
#define WL_OUT    4096

static struct {
    int      mode;
    int      fd;
    uint8_t  type[WL_OBJS];
    uint8_t  in[WL_IN];
    size_t   inlen;
    uint8_t  out[WL_OUT];
    size_t   outlen;
    int      dead;

    uint32_t compositor, shm, wm, seat, relmgr, constraints;
    uint32_t seat_ver;
    uint32_t keyboard, pointer, rel, locked;
    int      sync_done;

    uint32_t w, h, req_w, req_h, fh;
    char     title[64];
    uint32_t pool, buffer, surface, xsurf, toplevel;
    int      shm_fd;
    uint32_t *base, *pixels;
    int32_t  px, py;
    int      in_bar, close_hot;
    size_t   bytes;
    int      configured, frame_pending, dirty, open;

    uint32_t enter_serial;
    int      pointer_in, grab, focused;
    int32_t  rel_fx, rel_fy;
    uint8_t  held[128];

    cervus_fb_event_t q[WL_EVQ];
    int      qh, qt;
} W = { .mode = -1, .fd = -1, .shm_fd = -1 };

static uint32_t wl_new(int type) {
    for (uint32_t id = 2; id < WL_OBJS; id++)
        if (W.type[id] == O_FREE) { W.type[id] = (uint8_t)type; return id; }
    W.dead = 1;
    return 0;
}

static void ev_push(uint32_t type, uint32_t code, int32_t value, int32_t x, int32_t y) {
    int next = (W.qh + 1) % WL_EVQ;
    if (next == W.qt) return;
    W.q[W.qh] = (cervus_fb_event_t){ type, code, value, x, y };
    W.qh = next;
}

static int wl_flush(void) {
    size_t off = 0;
    while (off < W.outlen) {
        ssize_t n = send(W.fd, W.out + off, W.outlen - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            W.dead = 1;
            W.outlen = 0;
            return -1;
        }
        off += (size_t)n;
    }
    W.outlen = 0;
    return 0;
}

typedef struct { uint8_t b[512]; size_t n; } msg_t;

static void m_u32(msg_t *m, uint32_t v) {
    if (m->n + 4 <= sizeof m->b) { memcpy(m->b + m->n, &v, 4); m->n += 4; }
}

static void m_str(msg_t *m, const char *s) {
    uint32_t len = (uint32_t)strlen(s) + 1;
    m_u32(m, len);
    uint32_t pad = (len + 3) & ~3u;
    if (m->n + pad > sizeof m->b) return;
    memset(m->b + m->n, 0, pad);
    memcpy(m->b + m->n, s, len);
    m->n += pad;
}

static void wl_send(uint32_t id, uint32_t op, msg_t *m, int fd) {
    uint32_t hdr[2] = { id, (uint32_t)((8 + m->n) << 16) | op };
    if (fd >= 0) {
        wl_flush();
        uint8_t buf[520];
        memcpy(buf, hdr, 8);
        memcpy(buf + 8, m->b, m->n);
        struct iovec iov = { buf, 8 + m->n };
        char cbuf[CMSG_SPACE(sizeof(int))];
        memset(cbuf, 0, sizeof cbuf);
        struct msghdr mh;
        memset(&mh, 0, sizeof mh);
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = cbuf;
        mh.msg_controllen = sizeof cbuf;
        struct cmsghdr *c = CMSG_FIRSTHDR(&mh);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &fd, sizeof(int));
        if (sendmsg(W.fd, &mh, 0) < 0) W.dead = 1;
        return;
    }
    if (W.outlen + 8 + m->n > sizeof W.out) wl_flush();
    memcpy(W.out + W.outlen, hdr, 8);
    memcpy(W.out + W.outlen + 8, m->b, m->n);
    W.outlen += 8 + m->n;
}

static void req0(uint32_t id, uint32_t op) {
    msg_t m = { .n = 0 };
    wl_send(id, op, &m, -1);
}

static void req1(uint32_t id, uint32_t op, uint32_t a) {
    msg_t m = { .n = 0 };
    m_u32(&m, a);
    wl_send(id, op, &m, -1);
}

static uint32_t wl_bind(uint32_t name, const char *iface, uint32_t ver, int type) {
    uint32_t id = wl_new(type);
    if (!id) return 0;
    msg_t m = { .n = 0 };
    m_u32(&m, name);
    m_str(&m, iface);
    m_u32(&m, ver);
    m_u32(&m, id);
    wl_send(2, 0, &m, -1);
    return id;
}

static void hide_cursor(void) {
    if (!W.pointer || !W.pointer_in) return;
    msg_t m = { .n = 0 };
    m_u32(&m, W.enter_serial);
    m_u32(&m, 0);
    m_u32(&m, 0);
    m_u32(&m, 0);
    wl_send(W.pointer, 0, &m, -1);
}

static void release_held(void) {
    for (uint32_t k = 0; k < sizeof W.held * 8; k++) {
        if (!(W.held[k / 8] & (1u << (k % 8)))) continue;
        W.held[k / 8] &= (uint8_t)~(1u << (k % 8));
        ev_push(CERVUS_FBEV_KEY, k, 0, 0, 0);
    }
}

static void commit_now(void);

static void deco_fill(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t c) {
    for (uint32_t y = y0; y < y1; y++)
        for (uint32_t x = x0; x < x1 && x < W.w; x++) W.base[(size_t)y * W.w + x] = c;
}

static void deco_text(int x, int y, const char *t, uint32_t c) {
    for (; *t; t++, x += 8) {
        unsigned ch = (unsigned char)*t;
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        const uint8_t *g = deco_font + (ch - 0x20) * 16;
        for (int r = 0; r < 16; r++)
            for (int b = 0; b < 8; b++)
                if ((g[r] & (0x80 >> b)) && x + b >= 0 && (uint32_t)(x + b) < W.w)
                    W.base[(size_t)(y + r) * W.w + (uint32_t)(x + b)] = c;
    }
}

static void deco_draw(void) {
    if (!W.base) return;
    uint32_t bg = W.focused ? 0xFFE6E6EA : 0xFFC9C9CF;
    uint32_t fg = W.focused ? 0xFF1C1C22 : 0xFF6A6A72;
    deco_fill(0, 0, W.w, DECO_H - 1, bg);
    deco_fill(0, DECO_H - 1, W.w, DECO_H, 0xFF9A9AA2);

    uint32_t cx = W.w > DECO_H ? W.w - DECO_H : 0;
    if (W.close_hot) deco_fill(cx, 0, W.w, DECO_H - 1, 0xFFD9443A);
    uint32_t xc = W.close_hot ? 0xFFFFFFFF : fg;
    for (int i = -5; i <= 5; i++) {
        uint32_t yy = (uint32_t)(DECO_H / 2 - 1 + i);
        uint32_t x0 = cx + DECO_H / 2 + (uint32_t)i;
        uint32_t x1 = cx + DECO_H / 2 - (uint32_t)i;
        W.base[(size_t)yy * W.w + x0] = xc;
        W.base[(size_t)yy * W.w + x0 - 1] = xc;
        W.base[(size_t)yy * W.w + x1] = xc;
        W.base[(size_t)yy * W.w + x1 - 1] = xc;
    }

    char t[64];
    int room = (int)(cx / 8) - 2;
    if (room < 1) return;
    int n = (int)strlen(W.title);
    if (n > room) n = room;
    memcpy(t, W.title, (size_t)n);
    t[n] = 0;
    int tx = ((int)W.w - n * 8) / 2;
    if (tx + n * 8 > (int)cx - 4) tx = (int)cx - 4 - n * 8;
    if (tx < 4) tx = 4;
    deco_text(tx, (DECO_H - 16) / 2, t, fg);
}

static void deco_pointer(int32_t x, int32_t y) {
    W.px = x;
    W.py = y;
    W.in_bar = y < DECO_H;
    int hot = W.in_bar && x >= (int32_t)W.w - DECO_H;
    if (hot != W.close_hot) {
        W.close_hot = hot;
        deco_draw();
        W.dirty = 1;
    }
}

static void on_event(uint32_t id, uint32_t op, const uint8_t *a, size_t len) {
    uint32_t u[8] = { 0 };
    for (size_t i = 0; i < 8 && (i + 1) * 4 <= len; i++) memcpy(&u[i], a + i * 4, 4);
    uint8_t t = id < WL_OBJS ? W.type[id] : O_FREE;

    switch (t) {
    case O_DISPLAY:
        if (op == 0) {
            W.dead = 1;
        } else if (op == 1 && u[0] < WL_OBJS) {
            W.type[u[0]] = O_FREE;
        }
        break;
    case O_REGISTRY:
        if (op == 0 && len >= 12) {
            uint32_t name = u[0], slen = u[1];
            if (8 + slen > len) break;
            const char *iface = (const char *)a + 8;
            uint32_t ver;
            memcpy(&ver, a + 8 + ((slen + 3) & ~3u), 4);
            if (!strcmp(iface, "wl_compositor") && !W.compositor)
                W.compositor = wl_bind(name, iface, ver < 4 ? ver : 4, O_COMPOSITOR);
            else if (!strcmp(iface, "wl_shm") && !W.shm)
                W.shm = wl_bind(name, iface, 1, O_SHM);
            else if (!strcmp(iface, "xdg_wm_base") && !W.wm)
                W.wm = wl_bind(name, iface, 1, O_WM);
            else if (!strcmp(iface, "wl_seat") && !W.seat) {
                W.seat_ver = ver < 5 ? ver : 5;
                W.seat = wl_bind(name, iface, W.seat_ver, O_SEAT);
            } else if (!strcmp(iface, "zwp_relative_pointer_manager_v1") && !W.relmgr)
                W.relmgr = wl_bind(name, iface, 1, O_RELMGR);
            else if (!strcmp(iface, "zwp_pointer_constraints_v1") && !W.constraints)
                W.constraints = wl_bind(name, iface, 1, O_CONSTRAINTS);
        }
        break;
    case O_SYNC:
        if (op == 0) W.sync_done = 1;
        break;
    case O_FRAME:
        if (op == 0) {
            W.frame_pending = 0;
            if (W.dirty) commit_now();
        }
        break;
    case O_WM:
        if (op == 0) req1(W.wm, 3, u[0]);
        break;
    case O_XSURF:
        if (op == 0) {
            req1(W.xsurf, 4, u[0]);
            if (!W.configured) { W.configured = 1; deco_draw(); W.dirty = 1; }
            if (W.dirty && !W.frame_pending) commit_now();
        }
        break;
    case O_TOPLEVEL:
        if (op == 1) ev_push(CERVUS_FBEV_CLOSE, 0, 0, 0, 0);
        break;
    case O_SEAT:
        if (op == 0) {
            if ((u[0] & 2) && !W.keyboard) {
                W.keyboard = wl_new(O_KEYBOARD);
                if (W.keyboard) req1(W.seat, 1, W.keyboard);
            }
            if ((u[0] & 1) && !W.pointer) {
                W.pointer = wl_new(O_POINTER);
                if (W.pointer) req1(W.seat, 0, W.pointer);
                if (W.pointer && W.relmgr && !W.rel) {
                    W.rel = wl_new(O_REL);
                    msg_t m = { .n = 0 };
                    m_u32(&m, W.rel);
                    m_u32(&m, W.pointer);
                    wl_send(W.relmgr, 1, &m, -1);
                }
            }
        }
        break;
    case O_KEYBOARD:
        if (op == 1) {
            W.focused = 1;
            deco_draw();
            W.dirty = 1;
            ev_push(CERVUS_FBEV_FOCUS, 0, 1, 0, 0);
        } else if (op == 2) {
            W.focused = 0;
            release_held();
            deco_draw();
            W.dirty = 1;
            ev_push(CERVUS_FBEV_FOCUS, 0, 0, 0, 0);
        } else if (op == 3 && len >= 16) {
            uint32_t key = u[2], state = u[3];
            if (key < sizeof W.held * 8) {
                if (state) W.held[key / 8] |= (uint8_t)(1u << (key % 8));
                else       W.held[key / 8] &= (uint8_t)~(1u << (key % 8));
            }
            ev_push(CERVUS_FBEV_KEY, key, state ? 1 : 0, 0, 0);
        }
        break;
    case O_POINTER:
        if (op == 0) {
            W.enter_serial = u[0];
            W.pointer_in = 1;
            if (W.grab) hide_cursor();
            deco_pointer((int32_t)u[2] / 256, (int32_t)u[3] / 256);
            if (!W.in_bar) ev_push(CERVUS_FBEV_MOTION, 0, 0, W.px, W.py - DECO_H);
        } else if (op == 1) {
            W.pointer_in = 0;
            deco_pointer(-1, DECO_H);
        } else if (op == 2) {
            deco_pointer((int32_t)u[1] / 256, (int32_t)u[2] / 256);
            if (!W.in_bar) ev_push(CERVUS_FBEV_MOTION, 0, 0, W.px, W.py - DECO_H);
        } else if (op == 3) {
            if (W.in_bar && !W.grab) {
                if (u[3] && W.close_hot) {
                    ev_push(CERVUS_FBEV_CLOSE, 0, 0, 0, 0);
                } else if (u[3] && u[2] == 0x110 && W.toplevel && W.seat) {
                    msg_t m = { .n = 0 };
                    m_u32(&m, W.seat);
                    m_u32(&m, u[0]);
                    wl_send(W.toplevel, 5, &m, -1);
                    wl_flush();
                }
            } else {
                ev_push(CERVUS_FBEV_BUTTON, u[2], u[3] ? 1 : 0, 0, 0);
            }
        } else if (op == 4) {
            int32_t v = (int32_t)u[2];
            if (u[1] == 0 && v) ev_push(CERVUS_FBEV_WHEEL, 0, v > 0 ? 1 : -1, 0, 0);
        }
        break;
    case O_REL:
        if (op == 0 && len >= 16) {
            W.rel_fx += (int32_t)u[2];
            W.rel_fy += (int32_t)u[3];
            int32_t dx = W.rel_fx / 256, dy = W.rel_fy / 256;
            W.rel_fx -= dx * 256;
            W.rel_fy -= dy * 256;
            if (dx || dy) ev_push(CERVUS_FBEV_RELMOVE, 0, 0, dx, dy);
        }
        break;
    default:
        break;
    }
}

static int wl_read(int block) {
    if (W.fd < 0 || W.dead) return -1;
    if (!block) {
        struct pollfd p = { W.fd, POLLIN, 0 };
        if (poll(&p, 1, 0) <= 0 || !(p.revents & (POLLIN | POLLHUP | POLLERR))) return 0;
    }
    char cbuf[CMSG_SPACE(sizeof(int) * 8)];
    struct iovec iov = { W.in + W.inlen, sizeof W.in - W.inlen };
    struct msghdr mh;
    memset(&mh, 0, sizeof mh);
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = cbuf;
    mh.msg_controllen = sizeof cbuf;
    ssize_t n = recvmsg(W.fd, &mh, 0);
    if (n <= 0) {
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) return 0;
        W.dead = 1;
        return -1;
    }
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) continue;
        size_t nfd = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        for (size_t i = 0; i < nfd; i++) {
            int fd;
            memcpy(&fd, CMSG_DATA(c) + i * sizeof(int), sizeof(int));
            close(fd);
        }
    }
    W.inlen += (size_t)n;
    size_t off = 0;
    while (W.inlen - off >= 8) {
        uint32_t id, so;
        memcpy(&id, W.in + off, 4);
        memcpy(&so, W.in + off + 4, 4);
        uint32_t size = so >> 16;
        if (size < 8) { W.dead = 1; break; }
        if (W.inlen - off < size) break;
        on_event(id, so & 0xFFFF, W.in + off + 8, size - 8);
        off += size;
    }
    memmove(W.in, W.in + off, W.inlen - off);
    W.inlen -= off;
    return 1;
}

static void wl_roundtrip(void) {
    uint32_t cb = wl_new(O_SYNC);
    if (!cb) return;
    W.sync_done = 0;
    req1(1, 0, cb);
    wl_flush();
    while (!W.sync_done && !W.dead) if (wl_read(1) < 0) break;
}

static int wl_connect(void) {
    const char *disp = getenv("WAYLAND_DISPLAY");
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!disp || !*disp) return -1;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    if (disp[0] == '/') snprintf(sa.sun_path, sizeof sa.sun_path, "%s", disp);
    else if (dir && *dir) snprintf(sa.sun_path, sizeof sa.sun_path, "%s/%s", dir, disp);
    else return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(fd); return -1; }
    W.fd = fd;
    W.type[1] = O_DISPLAY;
    W.type[2] = O_REGISTRY;
    req1(1, 1, 2);
    wl_roundtrip();
    wl_roundtrip();
    if (W.dead || !W.compositor || !W.shm || !W.wm) {
        close(fd);
        W.fd = -1;
        return -1;
    }
    return 0;
}

int cervus_fb_windowed(void) {
    if (W.mode < 0) W.mode = wl_connect() == 0 ? 1 : 0;
    return W.mode;
}

void cervus_fb_set_size(unsigned w, unsigned h) {
    W.req_w = w;
    W.req_h = h;
    if (!W.open) W.w = W.h = 0;
}

void cervus_fb_set_title(const char *title) {
    snprintf(W.title, sizeof W.title, "%s", title ? title : "");
    if (W.open && W.toplevel) {
        msg_t m = { .n = 0 };
        m_str(&m, W.title);
        wl_send(W.toplevel, 2, &m, -1);
        deco_draw();
        W.dirty = 1;
        if (!W.frame_pending) commit_now();
        wl_flush();
    }
}

static void pick_size(void) {
    if (W.w) return;
    if (W.req_w && W.req_h) { W.w = W.req_w; W.h = W.req_h; return; }
    const char *e = getenv("CERVUS_FB_SIZE");
    unsigned ew = 0, eh = 0;
    if (e && sscanf(e, "%ux%u", &ew, &eh) == 2 && ew >= 64 && eh >= 64 && ew <= 4096 && eh <= 4096) {
        W.w = ew; W.h = eh;
        return;
    }
    cervus_fb_info_t s;
    if (cervus_display_info(&s) == 0 && (s.width < 900 || s.height < 700)) { W.w = 640; W.h = 480; }
    else { W.w = 800; W.h = 600; }
}

static void commit_now(void) {
    if (!W.open || !W.configured) return;
    msg_t m = { .n = 0 };
    m_u32(&m, W.buffer);
    m_u32(&m, 0);
    m_u32(&m, 0);
    wl_send(W.surface, 1, &m, -1);
    m.n = 0;
    m_u32(&m, 0); m_u32(&m, 0); m_u32(&m, W.w); m_u32(&m, W.fh);
    wl_send(W.surface, 2, &m, -1);
    uint32_t cb = wl_new(O_FRAME);
    if (cb) { req1(W.surface, 3, cb); W.frame_pending = 1; }
    req0(W.surface, 6);
    W.dirty = 0;
    wl_flush();
}

static int window_open(void) {
    if (W.open) return 0;
    if (!cervus_fb_windowed()) return -ENODEV;
    pick_size();
    W.fh = W.h + DECO_H;
    W.bytes = (size_t)W.w * W.fh * 4;
    W.shm_fd = memfd_create("cervus-fb", 0);
    if (W.shm_fd < 0 || ftruncate(W.shm_fd, (off_t)W.bytes) != 0) return -ENOMEM;
    W.base = mmap(NULL, W.bytes, PROT_READ | PROT_WRITE, MAP_SHARED, W.shm_fd, 0);
    if (W.base == MAP_FAILED) { W.base = W.pixels = NULL; return -ENOMEM; }
    memset(W.base, 0, W.bytes);
    W.pixels = W.base + (size_t)W.w * DECO_H;
    W.close_hot = 0;
    W.in_bar = 0;

    W.pool = wl_new(O_POOL);
    msg_t m = { .n = 0 };
    m_u32(&m, W.pool);
    m_u32(&m, (uint32_t)W.bytes);
    wl_send(W.shm, 0, &m, W.shm_fd);

    W.buffer = wl_new(O_BUFFER);
    m.n = 0;
    m_u32(&m, W.buffer); m_u32(&m, 0); m_u32(&m, W.w); m_u32(&m, W.fh);
    m_u32(&m, W.w * 4); m_u32(&m, 1);
    wl_send(W.pool, 0, &m, -1);

    W.surface = wl_new(O_SURFACE);
    req1(W.compositor, 0, W.surface);
    W.xsurf = wl_new(O_XSURF);
    m.n = 0;
    m_u32(&m, W.xsurf); m_u32(&m, W.surface);
    wl_send(W.wm, 2, &m, -1);
    W.toplevel = wl_new(O_TOPLEVEL);
    req1(W.xsurf, 1, W.toplevel);

    if (!W.title[0]) snprintf(W.title, sizeof W.title, "%s",
                              program_invocation_short_name && *program_invocation_short_name
                              ? program_invocation_short_name : "cervus");
    m.n = 0; m_str(&m, W.title); wl_send(W.toplevel, 2, &m, -1);
    m.n = 0; m_str(&m, W.title); wl_send(W.toplevel, 3, &m, -1);
    m.n = 0; m_u32(&m, W.w); m_u32(&m, W.fh); wl_send(W.toplevel, 7, &m, -1);
    m.n = 0; m_u32(&m, W.w); m_u32(&m, W.fh); wl_send(W.toplevel, 8, &m, -1);
    req0(W.surface, 6);
    wl_flush();

    W.open = 1;
    W.configured = 0;
    W.frame_pending = 0;
    while (!W.configured && !W.dead) if (wl_read(1) < 0) break;
    return W.dead ? -EIO : 0;
}

static void window_close(void) {
    if (!W.open) return;
    cervus_fb_grab_pointer(0);
    req0(W.toplevel, 0);
    req0(W.xsurf, 0);
    req0(W.surface, 0);
    req0(W.buffer, 0);
    req0(W.pool, 1);
    wl_flush();
    W.type[W.toplevel] = W.type[W.xsurf] = W.type[W.surface] = O_ZOMBIE;
    W.type[W.buffer] = W.type[W.pool] = O_ZOMBIE;
    W.toplevel = W.xsurf = W.surface = W.buffer = W.pool = 0;
    munmap(W.base, W.bytes);
    close(W.shm_fd);
    W.base = W.pixels = NULL;
    W.shm_fd = -1;
    W.open = 0;
    W.configured = 0;
    W.w = W.h = 0;
    W.req_w = W.req_h = 0;
}

int cervus_fb_info(cervus_fb_info_t *out) {
    if (!cervus_fb_windowed()) return (int)syscall1(SYS_FB_INFO, out);
    if (!out) return -EINVAL;
    pick_size();
    memset(out, 0, sizeof *out);
    out->width = W.w;
    out->height = W.h;
    out->pitch = W.w * 4;
    out->bpp = 32;
    out->size_bytes = (uint64_t)W.w * W.h * 4;
    return 0;
}

long cervus_fb_blit(const void *buf, unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!cervus_fb_windowed()) return (long)syscall5(SYS_FB_BLIT, buf, x, y, w, h);
    if (!W.open) return -EBUSY;
    if (x >= W.w || y >= W.h || !buf) return -EINVAL;
    unsigned cw = x + w > W.w ? W.w - x : w;
    unsigned ch = y + h > W.h ? W.h - y : h;
    const uint32_t *src = buf;
    for (unsigned r = 0; r < ch; r++)
        memcpy(W.pixels + (size_t)(y + r) * W.w + x, src + (size_t)r * w, (size_t)cw * 4);
    W.dirty = 1;
    wl_read(0);
    if (!W.frame_pending) commit_now();
    return (long)w * h * 4;
}

void *cervus_fb_map(void) {
    if (!cervus_fb_windowed()) {
        uint64_t addr = 0;
        long r = (long)syscall1(SYS_FB_MAP, &addr);
        if (r < 0) return (void *)0;
        return (void *)(uintptr_t)addr;
    }
    if (window_open() != 0) return (void *)0;
    return W.pixels;
}

int cervus_fb_acquire(void) {
    if (!cervus_fb_windowed()) return (int)syscall0(SYS_FB_ACQUIRE);
    return window_open();
}

int cervus_fb_release(void) {
    if (!cervus_fb_windowed()) return (int)syscall0(SYS_FB_RELEASE);
    window_close();
    return 0;
}

int cervus_fb_present(void) {
    if (!cervus_fb_windowed()) return 0;
    if (!W.open) return -EBUSY;
    W.dirty = 1;
    wl_read(0);
    if (!W.frame_pending) commit_now();
    return W.dead ? -EIO : 0;
}

int cervus_fb_poll_event(cervus_fb_event_t *ev) {
    if (!cervus_fb_windowed() || !ev) return 0;
    if (W.qh == W.qt) {
        while (wl_read(0) > 0) { }
        if (W.dirty && !W.frame_pending) commit_now();
    }
    if (W.qh == W.qt) {
        if (W.dead) { *ev = (cervus_fb_event_t){ CERVUS_FBEV_CLOSE, 0, 0, 0, 0 }; return 1; }
        return 0;
    }
    *ev = W.q[W.qt];
    W.qt = (W.qt + 1) % WL_EVQ;
    return 1;
}

int cervus_fb_wait_event(int timeout_ms) {
    if (!cervus_fb_windowed()) return 0;
    if (W.qh != W.qt) return 1;
    struct pollfd p = { W.fd, POLLIN, 0 };
    int r = poll(&p, 1, timeout_ms);
    if (r > 0) wl_read(0);
    return W.qh != W.qt || W.dead;
}

int cervus_fb_grab_pointer(int on) {
    if (!cervus_fb_windowed() || !W.open) return -ENODEV;
    if (on && !W.grab) {
        if (!W.constraints || !W.pointer) return -ENOTSUP;
        W.locked = wl_new(O_LOCKED);
        msg_t m = { .n = 0 };
        m_u32(&m, W.locked);
        m_u32(&m, W.surface);
        m_u32(&m, W.pointer);
        m_u32(&m, 0);
        m_u32(&m, 2);
        wl_send(W.constraints, 1, &m, -1);
        W.grab = 1;
        hide_cursor();
        wl_flush();
    } else if (!on && W.grab) {
        if (W.locked) { req0(W.locked, 0); W.type[W.locked] = O_ZOMBIE; W.locked = 0; }
        W.grab = 0;
        wl_flush();
    }
    return 0;
}
