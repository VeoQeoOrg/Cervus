#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/pmm.h"
#include <limine.h>
#include <string.h>

extern struct limine_framebuffer *global_framebuffer;
extern void console_set_offscreen(int on);

int64_t sys_fb_info(uint64_t info_ptr)
{
    struct limine_framebuffer *fb = global_framebuffer;
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
    struct limine_framebuffer *fb = global_framebuffer;
    if (!fb) return -ENODEV;
    if (!buf_ptr) return -EINVAL;

    if (x >= fb->width || y >= fb->height) return -EINVAL;
    if (w == 0 || h == 0) return 0;
    if (x + w > fb->width)  w = fb->width  - x;
    if (y + h > fb->height) h = fb->height - y;

    if (!syscall_uptr_validate((void *)buf_ptr, w * h * 4)) return -EFAULT;

    uint32_t fb_pitch = (uint32_t)(fb->pitch / 4);
    const uint32_t *src = (const uint32_t *)buf_ptr;
    uint32_t *dst = (uint32_t *)fb->address;

    for (uint64_t row = 0; row < h; row++) {
        uint32_t *drow = dst + (y + row) * fb_pitch + x;
        const uint32_t *srow = src + row * w;
        memcpy(drow, srow, (size_t)w * 4);
    }
    return (int64_t)(w * h * 4);
}

int64_t sys_fb_map(uint64_t out_addr_ptr)
{
    struct limine_framebuffer *fb = global_framebuffer;
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

    uint64_t vf = VMM_PRESENT | VMM_USER | VMM_WRITE | VMM_NOEXEC;
    for (uint64_t i = 0; i < pages; i++) {
        if (!vmm_map_page(t->pagemap, uaddr + i * 0x1000, phys + i * 0x1000, vf))
            return -ENOMEM;
    }

    return syscall_copy_to_user((void *)out_addr_ptr, &uaddr, sizeof(uaddr));
}

int64_t sys_fb_acquire(void)
{
    console_set_offscreen(1);
    return 0;
}

int64_t sys_fb_release(void)
{
    extern void console_force_full_redraw(void);
    console_set_offscreen(0);
    console_force_full_redraw();
    return 0;
}
