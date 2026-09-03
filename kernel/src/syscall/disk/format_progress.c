#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/drivers/disk/disk.h"

int64_t sys_disk_format_progress(uint64_t a1, uint64_t a2, uint64_t a3,
                                 uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return (int64_t)fmt_progress_get();
}
