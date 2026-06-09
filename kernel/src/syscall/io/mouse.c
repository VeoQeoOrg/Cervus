#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/mouse.h"

int64_t sys_mouse_state(uint64_t buf_ptr)
{
    if (!buf_ptr) return -EINVAL;

    const mouse_state_t *m = mouse_get_state();
    if (!m) return -ENODEV;

    cervus_mouse_info_t info;
    info.x          = m->x;
    info.y          = m->y;
    info.btn_left   = m->btn_left   ? 1 : 0;
    info.btn_right  = m->btn_right  ? 1 : 0;
    info.btn_middle = m->btn_middle ? 1 : 0;
    info.scroll     = (m->scroll == MOUSE_SCROLL_UP)   ?  1 :
                      (m->scroll == MOUSE_SCROLL_DOWN) ? -1 : 0;

    return syscall_copy_to_user((void *)buf_ptr, &info, sizeof(info));
}
