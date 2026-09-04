#include "../../../include/graphics/fb/fb.h"
#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include <string.h>

extern fb_info_t *global_framebuffer;
extern void console_set_offscreen(int on);
extern int  vt_fb_may_draw(int vt);
extern void vt_fb_acquire(int vt);
extern void vt_fb_release(int vt);

static int caller_vt(void) {
    task_t *t = syscall_cur_task();
    return t ? t->ctty : 0;
}

int64_t sys_fb_info(uint64_t info_ptr)
{
    fb_info_t *fb = global_framebuffer;
    if (!fb) return -ENODEV;
    if (!info_ptr) return -EINVAL;
    if (!syscall_uptr_validate((void *)info_ptr, sizeof(cervus_fb_info_t))) return -EFAULT;

    cervus_fb_info_t out;
    out.width      = (uint32_t)fb->width;
    out.height     = (uint32_t)fb->height;
    out.pitch      = (uint32_t)fb->pitch;
    out.bpp        = (uint32_t)fb->bpp;
    out.phys_addr  = (uint64_t)((uintptr_t)fb->address - pmm_get_hhdm_offset());
    out.size_bytes = (uint64_t)fb->pitch * fb->height;
    return syscall_copy_to_user((void *)info_ptr, &out, sizeof(out));
}

int64_t sys_fb_blit(uint64_t buf_ptr, uint64_t x, uint64_t y, uint64_t w, uint64_t h)
{
    fb_info_t *fb = global_framebuffer;
    if (!fb) return -ENODEV;
    if (!buf_ptr) return -EINVAL;

    if (x >= fb->width || y >= fb->height) return -EINVAL;
    if (w == 0 || h == 0) return 0;
    if (x + w > fb->width)  w = fb->width  - x;
    if (y + h > fb->height) h = fb->height - y;

    if (!syscall_uptr_validate((void *)buf_ptr, w * h * 4)) return -EFAULT;

    if (!vt_fb_may_draw(caller_vt())) return (int64_t)(w * h * 4);

    extern uint32_t *fb_get_backbuffer(void);
    extern uint32_t  fb_backbuffer_pitch(void);
    extern void      fb_flush_lines(fb_info_t *fb, uint32_t y_start, uint32_t y_end);

    const uint32_t *src = (const uint32_t *)buf_ptr;
    uint32_t *bb = fb_get_backbuffer();

    if (bb) {
        uint32_t bpitch = fb_backbuffer_pitch();
        for (uint64_t row = 0; row < h; row++) {
            uint32_t *drow = bb + (y + row) * bpitch + x;
            memcpy(drow, src + row * w, (size_t)w * 4);
        }
        fb_flush_lines(fb, (uint32_t)y, (uint32_t)(y + h));
    } else {
        uint32_t fb_pitch = (uint32_t)(fb->pitch / 4);
        uint32_t *dst = (uint32_t *)fb->address;
        for (uint64_t row = 0; row < h; row++)
            memcpy(dst + (y + row) * fb_pitch + x, src + row * w, (size_t)w * 4);
    }
    return (int64_t)(w * h * 4);
}

int64_t sys_fb_map(uint64_t out_addr_ptr)
{
    fb_info_t *fb = global_framebuffer;
    if (!fb) return -ENODEV;
    if (!out_addr_ptr) return -EINVAL;
    if (!syscall_uptr_validate((void *)out_addr_ptr, sizeof(uint64_t))) return -EFAULT;

    task_t *t = syscall_cur_task();
    if (!t || !t->pagemap) return -EPERM;

    uint64_t fb_bytes = (uint64_t)fb->pitch * fb->height;
    uint64_t pages = (fb_bytes + 0xFFF) >> 12;
    uintptr_t phys = (uintptr_t)fb->address - pmm_get_hhdm_offset();
    phys &= ~0xFFFULL;

    uint64_t span = pages * 0x1000;
    if (t->brk_max < span) return -ENOMEM;
    uintptr_t uaddr = (t->brk_max - span) & ~0xFFFULL;
    if (uaddr <= t->brk_current) return -ENOMEM;
    t->brk_max = uaddr;

    uint64_t vf = VMM_PRESENT | VMM_USER | VMM_WRITE | VMM_NOEXEC | VMM_SHARED;
    for (uint64_t i = 0; i < pages; i++) {
        if (!vmm_map_page(t->pagemap, uaddr + i * 0x1000, phys + i * 0x1000, vf))
            return -ENOMEM;
    }

    return syscall_copy_to_user((void *)out_addr_ptr, &uaddr, sizeof(uaddr));
}

int64_t sys_fb_acquire(void)
{
    vt_fb_acquire(caller_vt());
    return 0;
}

int64_t sys_fb_release(void)
{
    extern void console_force_full_redraw(void);
    int vt = caller_vt();
    vt_fb_release(vt);
    if (vt_fb_may_draw(vt)) console_force_full_redraw();
    return 0;
}

int64_t sys_setfont(uint64_t w, uint64_t h, uint64_t nglyph,
                    uint64_t glyphs_ptr, uint64_t cp2_ptr)
{
    extern void vt_font_changed(void);

    if (w == 0) {
        fb_font_reset();
        vt_font_changed();
        return 0;
    }
    if (w < 4 || w > FONT_MAX_W || h < 6 || h > FONT_MAX_H) return -EINVAL;
    if (nglyph == 0 || nglyph > 65536) return -EINVAL;

    size_t gbytes = (size_t)w * h * nglyph;
    if (gbytes > (8u * 1024u * 1024u)) return -EINVAL;
    size_t mbytes = (size_t)FONT_CP_MAP_SIZE * sizeof(uint16_t);

    if (!syscall_uptr_validate((void *)glyphs_ptr, gbytes)) return -EFAULT;
    if (!syscall_uptr_validate((void *)cp2_ptr, mbytes))    return -EFAULT;

    if (fb_set_font((uint16_t)w, (uint16_t)h, (uint32_t)nglyph,
                    (const uint8_t *)glyphs_ptr, (const uint16_t *)cp2_ptr) != 0)
        return -EINVAL;

    vt_font_changed();
    return 0;
}
